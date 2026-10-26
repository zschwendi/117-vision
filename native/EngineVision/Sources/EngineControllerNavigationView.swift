import SwiftUI
import GameController

@MainActor
struct EngineControllerMenuItem {
    var id: String
    var title: String
    var value = ""
    var enabled = true
    var activate: () -> Void
    var adjust: ((Int) -> Void)? = nil
    var signature: String { "\(id)|\(title)|\(value)|\(enabled)" }
    var focusItem: EngineControllerFocus.Item { .init(id: id, enabled: enabled) }
}

/// One native lease spans the visible app-control stack. The raw controller
/// reader stays shared with the engine; while the lease (or its post-close
/// release wait) is held, native engine delivery is neutral for Sense and
/// ordinary gamepads alike (gamecontroller.m / dinput8.c).
@MainActor
final class EngineAppControllerNavigation: ObservableObject {
    static let shared = EngineAppControllerNavigation()
    @Published private(set) var selectedID: String?
    @Published private(set) var activeOwner: UUID?
    /// A Sense pair or ordinary gamepad that can drive app menus.
    @Published private(set) var navigationConnected = false
    /// Selection is drawn only while a controller is navigating.
    @Published private(set) var highlightVisible = false
    @Published private(set) var presence = EngineControllerPresence.checking
    private struct Surface {
        var id: UUID
        var items: [EngineControllerMenuItem]
        var back: () -> Void
        var focus = EngineControllerFocus()
    }
    private var surfaces: [Surface] = []
    private var token: UInt64 = 0
    private var input = EngineControllerNavigationInput()
    private var timer: Timer?
    private var entry = EngineControllerEntryChord()
    private var pendingUntil: Double?
    private var applicationActive = true
    private var highlight = EngineControllerHighlight()
    private var presenceKey: (connected: Bool, spatial: Bool, source: UInt64)?
    private var presenceCheckedAt = -Double.infinity
    var onOpenControls: (() -> Void)?

    func setApplicationActive(_ active: Bool) {
        guard applicationActive != active else { return }
        applicationActive = active; input.reset(); entry = EngineControllerEntryChord()
        highlight.reset(); publishHighlight()
        if !active {
            pendingUntil = nil
            if token != 0 { hostgc_app_input_end(token); token = 0 }
        }
    }
    func start() {
        guard timer == nil else { return }
        let pollTimer = Timer(timeInterval: 1.0 / 60.0, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.poll() }
        }
        timer = pollTimer
        // Keep the exclusive input lease renewed while SwiftUI scrolls or
        // tracks a control in a non-default run-loop mode.
        RunLoop.main.add(pollTimer, forMode: .common)
    }

    func register(_ id: UUID, items: [EngineControllerMenuItem], back: @escaping () -> Void) {
        if let index = surfaces.firstIndex(where: { $0.id == id }) {
            surfaces[index].items = items; surfaces[index].back = back
        } else { surfaces.append(Surface(id: id, items: items, back: back)); input.reset() }
        if token == 0 && applicationActive { token = hostgc_app_input_begin() }
        pendingUntil = nil
        start()
        publishFocus()
    }
    func remove(_ id: UUID) {
        guard surfaces.contains(where: { $0.id == id }) else { return }
        let previous = surfaces.last?.id
        surfaces.removeAll { $0.id == id }
        if previous != surfaces.last?.id { input.reset() }
        if surfaces.isEmpty {
            pendingUntil = nil
            if token != 0 { hostgc_app_input_end(token); token = 0 }
            highlight.reset(); publishHighlight()
        }
        publishFocus()
    }
    private func publishFocus() {
        guard let index = surfaces.indices.last else { activeOwner = nil; selectedID = nil; return }
        surfaces[index].focus.reconcile(surfaces[index].items.map(\.focusItem))
        activeOwner = surfaces[index].id
        selectedID = surfaces[index].focus.selected
    }
    private func poll() {
        guard applicationActive else { return }
        let now = ProcessInfo.processInfo.systemUptime
        var pad = HostGCSnapshot()
        let connected = hostgc_poll(&pad) && pad.connected
        let sense = connected && pad.spatial
        if navigationConnected != connected { navigationConnected = connected }
        updatePresence(connected: connected, spatial: pad.spatial, source: pad.source_generation, now: now)
        if !connected { highlight.reset() } else { _ = highlight.expire(now: now) }
        publishHighlight()
        let buttons = withUnsafeBytes(of: pad.buttons) { Array($0) }
        func down(_ index: Int) -> Bool { index >= 0 && index < buttons.count && buttons[index] != 0 }
        let entryNeutral = buttons.enumerated().allSatisfy { index, value in
            value == 0 || index == Int(HOSTGC_BTN_MENU) || index == Int(HOSTGC_BTN_OPTIONS)
        } && [pad.spatial_raw_lx, pad.spatial_raw_ly, pad.rx, pad.ry, pad.lt, pad.rt].allSatisfy { $0.isFinite && abs($0) <= 0.2 }
        if entry.update(source: pad.source_generation, spatial: sense,
                        create: down(HOSTGC_BTN_OPTIONS), options: down(HOSTGC_BTN_MENU),
                        otherwiseNeutral: entryNeutral, now: now), surfaces.isEmpty, let open = onOpenControls {
            token = hostgc_app_input_begin(); pendingUntil = now + 2; input.reset()
            highlight.reveal(now: now); publishHighlight(); open()
        }
        if surfaces.isEmpty {
            if let deadline = pendingUntil, now <= deadline {
                if hostgc_app_input_renew(token) == 0 { token = hostgc_app_input_begin() }
            } else if token != 0 { hostgc_app_input_end(token); token = 0; pendingUntil = nil }
            return
        }
        guard let index = surfaces.indices.last else { return }
        if token == 0 || hostgc_app_input_renew(token) == 0 {
            token = hostgc_app_input_begin(); input.reset()
        }
        // A/B and the D-pad navigate; anything else held is a blocker.
        let navigationButtons: Set<Int> = [Int(HOSTGC_BTN_A), Int(HOSTGC_BTN_B), Int(HOSTGC_BTN_DPAD_UP),
                                           Int(HOSTGC_BTN_DPAD_DOWN), Int(HOSTGC_BTN_DPAD_LEFT), Int(HOSTGC_BTN_DPAD_RIGHT)]
        let otherHeld = buttons.enumerated().contains { index, value in
            value != 0 && !navigationButtons.contains(index)
        } || !pad.lt.isFinite || !pad.rt.isFinite || !pad.rx.isFinite || !pad.ry.isFinite ||
            pad.lt < 0 || pad.rt < 0 || pad.lt > 0.2 || pad.rt > 0.2 || abs(pad.rx) > 0.2 || abs(pad.ry) > 0.2
        // Sense uses its raw left stick; an ordinary pad its left stick, with
        // a pressed D-pad direction taking precedence on that axis.
        var x = pad.spatial ? pad.spatial_raw_lx : pad.lx
        var y = pad.spatial ? pad.spatial_raw_ly : pad.ly
        if !pad.spatial {
            if pad.dpad_left != pad.dpad_right { x = pad.dpad_left ? -1 : 1 }
            if pad.dpad_up != pad.dpad_down { y = pad.dpad_up ? 1 : -1 }
        }
        let sample = EngineControllerNavigationInput.Sample(source: pad.source_generation,
            connected: connected, spatial: pad.spatial, x: x, y: y,
            activate: down(HOSTGC_BTN_A), back: down(HOSTGC_BTN_B), otherHeld: otherHeld)
        guard let event = input.update(sample, now: now) else { return }
        // The first input after touch or idle only reveals the selection.
        let acts = highlight.accept(event, now: now)
        publishHighlight()
        guard acts else { return }
        switch event {
        case .move(let direction):
            surfaces[index].focus.move(direction, in: surfaces[index].items.map(\.focusItem)); publishFocus()
        case .back:
            let back = surfaces[index].back; back()
        case .activate, .adjust:
            guard let item = surfaces[index].items.first(where: { $0.id == selectedID && $0.enabled }) else { return }
            if case .adjust(let direction) = event { item.adjust?(direction) }
            else { item.activate(); hostgc_play_haptic(0.16, 0.4) }
        }
    }
}

extension EngineAppControllerNavigation {
    /// Touch or gaze-and-pinch on an app surface hands focus display back to it.
    func pointerInteraction() {
        highlight.pointerInteraction(); publishHighlight()
    }
    private func publishHighlight() {
        if highlightVisible != highlight.visible { highlightVisible = highlight.visible }
    }
    /// Topology changes refresh at once; partial Sense pairs are rechecked
    /// each second because pairing can settle without a topology change.
    private func updatePresence(connected: Bool, spatial: Bool, source: UInt64, now: Double) {
        let key = (connected: connected, spatial: spatial, source: source)
        if let presenceKey, presenceKey == key, connected || now - presenceCheckedAt < 1 { return }
        presenceKey = key; presenceCheckedAt = now
        let diagnostic = hostgc_input_state().map { String(cString: $0) } ?? ""
        let next = EngineControllerPresence(connected: connected, spatial: spatial, diagnostic: diagnostic)
        if presence != next { presence = next }
    }
}

private struct EngineControllerSelectionKey: EnvironmentKey { static let defaultValue: String? = nil }
extension EnvironmentValues {
    var engineControllerSelection: String? {
        get { self[EngineControllerSelectionKey.self] }
        set { self[EngineControllerSelectionKey.self] = newValue }
    }
}
private struct EngineControllerTarget: ViewModifier {
    let id: String
    @Environment(\.engineControllerSelection) private var selected
    func body(content: Content) -> some View {
        content
            .padding(3)
            .background(selected == id ? EngineLibraryTheme.accent.opacity(0.14) : .clear, in: RoundedRectangle(cornerRadius: 10))
            .overlay { RoundedRectangle(cornerRadius: 10).stroke(selected == id ? EngineLibraryTheme.accent : .clear, lineWidth: 2) }
            .id(id)
    }
}
private struct EngineControllerNavigationModifier: ViewModifier {
    var items: [EngineControllerMenuItem]
    var enabled: Bool
    var back: () -> Void
    @State private var owner = UUID()
    @State private var appeared = false
    @Environment(\.scenePhase) private var scenePhase
    @ObservedObject private var controller = EngineAppControllerNavigation.shared
    private var selected: String? {
        controller.activeOwner == owner && controller.navigationConnected && controller.highlightVisible ? controller.selectedID : nil
    }
    func body(content: Content) -> some View {
        ScrollViewReader { scroll in
            content
                .environment(\.engineControllerSelection, selected)
                .safeAreaInset(edge: .bottom) {
                    if selected != nil {
                        VStack(spacing: 3) {
                            let sense = controller.presence.kind == .sense
                            Text(sense ? "Sense app controls" : "Controller app controls")
                                .font(.caption.bold()).foregroundStyle(EngineLibraryTheme.accent)
                            Text(sense ? "Left stick: choose / adjust · Cross: select · Circle: back"
                                       : "Left stick or D-pad: choose / adjust · A / Cross: select · B / Circle: back")
                                .font(.caption2).foregroundStyle(.secondary)
                        }
                        .frame(maxWidth: .infinity).padding(10).background(.regularMaterial)
                        .allowsHitTesting(false)
                    }
                }
                .handlesGameControllerEvents(matching: .gamepad)
                // Observes pinches without claiming them from the controls.
                .simultaneousGesture(TapGesture().onEnded { controller.pointerInteraction() })
                .onChange(of: selected) { _, value in
                    if let value { withAnimation(.easeOut(duration: 0.15)) { scroll.scrollTo(value, anchor: .center) } }
                }
        }
        .onAppear { appeared = true; update() }
        .onDisappear { appeared = false; controller.remove(owner) }
        .onChange(of: items.map(\.signature)) { _, _ in update() }
        .onChange(of: enabled) { _, _ in update() }
        .onChange(of: scenePhase) { _, _ in update() }
    }
    private func update() {
        if appeared && enabled && scenePhase == .active { controller.register(owner, items: items, back: back) }
        else { controller.remove(owner) }
    }
}
extension View {
    func senseControllerTarget(_ id: String) -> some View { modifier(EngineControllerTarget(id: id)) }
    func senseControllerNavigation(_ items: [EngineControllerMenuItem], enabled: Bool = true,
                                   back: @escaping () -> Void) -> some View {
        modifier(EngineControllerNavigationModifier(items: items, enabled: enabled, back: back))
    }
}
