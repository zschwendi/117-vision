import SwiftUI

/// Shared presentation tokens for launcher-owned screens (visionOS and iPad).
/// The rendered game keeps its own colors and geometry.
enum EngineLibraryTheme {
    /// The one visible product name for launcher-owned screens (visionOS and
    /// iPad). Rename the app here; bundle, container, cloud and Keychain
    /// identifiers and stored preference keys never change with it.
    static let productName = "117"
    static let background = Color(red: 0.008, green: 0.024, blue: 0.040)
    static let surface = Color(red: 0.030, green: 0.074, blue: 0.098)
    static let raisedSurface = Color(red: 0.050, green: 0.112, blue: 0.140)
    static let accent = Color(red: 0.42, green: 0.80, blue: 0.95)
    static let accentGlow = Color(red: 0.42, green: 0.93, blue: 0.70)
    static let success = Color(red: 0.55, green: 0.88, blue: 0.62)
    static let warning = Color(red: 1.0, green: 0.74, blue: 0.38)
}

/// Launcher-only atmosphere: a deep blue-green sky, slow stars and an
/// original geometric ring band. Procedural; no game artwork is used.
struct EngineLibraryBackground: View {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.scenePhase) private var scenePhase
    @State private var startedAt = Date.now

    var body: some View {
        GeometryReader { proxy in
            TimelineView(.animation(minimumInterval: 1.0 / 20.0,
                                    paused: reduceMotion || scenePhase != .active)) { timeline in
                let time = reduceMotion ? 0 : max(0, timeline.date.timeIntervalSince(startedAt))
                Canvas { context, size in
                    Self.draw(in: &context, size: size, time: time)
                }
                .frame(width: proxy.size.width, height: proxy.size.height)
            }
        }
        .background(EngineLibraryTheme.background)
        .clipped()
        .allowsHitTesting(false)
        .accessibilityHidden(true)
    }

    private static func draw(in context: inout GraphicsContext, size: CGSize, time: Double) {
        let extent = max(size.width, size.height, 1)
        let full = CGRect(origin: .zero, size: size)
        context.fill(Path(full), with: .color(EngineLibraryTheme.background))

        // Two slowly drifting nebula lobes: Halo blue above, ring-world green below.
        let drift = CGFloat(sin(time * 0.045)) * 0.03
        context.fill(Path(full), with: .radialGradient(
            Gradient(colors: [Color(red: 0.05, green: 0.20, blue: 0.30).opacity(0.85), .clear]),
            center: CGPoint(x: size.width * (0.30 + drift), y: size.height * 0.28),
            startRadius: 0, endRadius: extent * 0.62))
        context.fill(Path(full), with: .radialGradient(
            Gradient(colors: [Color(red: 0.04, green: 0.24, blue: 0.17).opacity(0.70), .clear]),
            center: CGPoint(x: size.width * (0.74 - drift), y: size.height * 0.80),
            startRadius: 0, endRadius: extent * 0.55))

        // Deterministic star field with a gentle twinkle.
        for index in 0..<96 {
            let x = CGFloat((index * 137 + 29) % 997) / 997 * size.width
            let y = CGFloat((index * 251 + 61) % 991) / 991 * size.height
            let radius: CGFloat = index.isMultiple(of: 9) ? 1.2 : 0.6
            let opacity = 0.10 + 0.16 * (1 + sin(time * 0.22 + Double(index) * 1.7)) / 2
            context.fill(Path(ellipseIn: CGRect(x: x - radius, y: y - radius, width: radius * 2, height: radius * 2)),
                         with: .color(.white.opacity(opacity)))
        }

        // An original ring: a tilted, foreshortened band seen edge-on, with a
        // lit inner surface and a slow highlight travelling along its arc.
        let center = CGPoint(x: size.width * 0.5, y: size.height * 0.56)
        let ringWidth = extent * 1.18
        let ringHeight = ringWidth * 0.17
        var band = context
        band.translateBy(x: center.x, y: center.y)
        band.rotate(by: .degrees(-9))
        let outer = CGRect(x: -ringWidth / 2, y: -ringHeight / 2, width: ringWidth, height: ringHeight)
        // Layered wide strokes give a soft glow without a per-frame blur pass.
        band.stroke(Path(ellipseIn: outer), with: .color(EngineLibraryTheme.accentGlow.opacity(0.035)), lineWidth: 30)
        band.stroke(Path(ellipseIn: outer), with: .color(EngineLibraryTheme.accent.opacity(0.06)), lineWidth: 12)
        let phase = CGFloat((time * 0.035).truncatingRemainder(dividingBy: 1))
        band.stroke(Path(ellipseIn: outer), with: .linearGradient(
            Gradient(stops: [.init(color: .clear, location: 0),
                             .init(color: EngineLibraryTheme.accent.opacity(0.18), location: max(0, phase - 0.25)),
                             .init(color: .white.opacity(0.42), location: phase),
                             .init(color: EngineLibraryTheme.accentGlow.opacity(0.22), location: min(1, phase + 0.25)),
                             .init(color: .clear, location: 1)]),
            startPoint: CGPoint(x: outer.minX, y: 0), endPoint: CGPoint(x: outer.maxX, y: 0)), lineWidth: 1.6)
        let inner = outer.insetBy(dx: ringWidth * 0.012, dy: ringHeight * 0.09)
        band.stroke(Path(ellipseIn: inner), with: .color(EngineLibraryTheme.accent.opacity(0.10)), lineWidth: 5)

        // Soft vignette keeps the shelf readable.
        context.fill(Path(full), with: .radialGradient(
            Gradient(colors: [.clear, .black.opacity(0.46)]),
            center: CGPoint(x: size.width / 2, y: size.height * 0.45),
            startRadius: extent * 0.28, endRadius: extent * 0.78))
    }
}

/// Brand capsule used as the launcher's top menu label.
struct EngineLibraryTitleLabel: View {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    private let shimmerDuration: TimeInterval = 2.8

    var body: some View {
        TimelineView(.animation(minimumInterval: 1.0 / 30.0, paused: reduceMotion)) { timeline in
            Text(EngineLibraryTheme.productName)
                .font(.caption.weight(.semibold))
                .foregroundStyle(foreground(at: timeline.date))
                .padding(.horizontal, 14)
                .padding(.vertical, 7)
                .background(EngineLibraryTheme.surface.opacity(0.92), in: Capsule())
                .overlay { Capsule().strokeBorder(EngineLibraryTheme.accent.opacity(0.28), lineWidth: 1) }
                .shadow(color: EngineLibraryTheme.accent.opacity(0.30), radius: 6)
        }
    }

    private func foreground(at date: Date) -> LinearGradient {
        guard !reduceMotion else {
            return LinearGradient(colors: [EngineLibraryTheme.accent], startPoint: .leading, endPoint: .trailing)
        }
        let wrapped = date.timeIntervalSinceReferenceDate.truncatingRemainder(dividingBy: shimmerDuration)
        let location = CGFloat(wrapped / shimmerDuration * 4 - 2)
        return LinearGradient(colors: [EngineLibraryTheme.accent, EngineLibraryTheme.accent, .white.opacity(0.88),
                                       EngineLibraryTheme.accentGlow, EngineLibraryTheme.accent],
                              startPoint: UnitPoint(x: location - 1, y: 0.5),
                              endPoint: UnitPoint(x: location + 1, y: 0.5))
    }
}

struct EngineLibraryCover: View {
    @ObservedObject private var artwork = EngineLibraryArtworkStore.shared

    var body: some View {
        GeometryReader { proxy in
            ZStack {
                if let image = artwork.image {
                    Color.black.opacity(0.8)
                    Image(uiImage: image)
                        .resizable()
                        .scaledToFit()
                        .frame(width: proxy.size.width, height: proxy.size.height)
                } else {
                    LinearGradient(colors: [EngineLibraryTheme.raisedSurface, EngineLibraryTheme.background],
                                   startPoint: .topLeading, endPoint: .bottomTrailing)
                    Text("1")
                        .font(.system(size: proxy.size.width * 0.68, weight: .ultraLight))
                        .foregroundStyle(.white.opacity(0.95))
                }
            }
            .frame(width: proxy.size.width, height: proxy.size.height)
            .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
            .overlay {
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .strokeBorder(EngineLibraryTheme.accent.opacity(0.5), lineWidth: 1)
            }
        }
        .accessibilityHidden(true)
    }
}

/// One centered shelf card. On visionOS the whole card is a gaze/pinch
/// target with a native hover effect; elsewhere it is a plain button.
struct EngineLibraryGameCard<Status: View>: View {
    let coverWidth: CGFloat
    let accessibilityLabel: String
    let enabled: Bool
    let action: () -> Void
    @ViewBuilder let status: () -> Status
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        // The artwork menu is a sibling of the launch target, so opening it
        // cannot also start the game. This matches the Dark Eco shelf affordance.
        ZStack(alignment: .topTrailing) {
            launchSurface
            EngineLibraryArtworkMenu()
                .padding(.top, coverWidth * 1.5 - 44)
                .padding(.trailing, 12)
        }
        .contextMenu { EngineLibraryArtworkActions() }
    }

    @ViewBuilder
    private var launchSurface: some View {
        #if os(visionOS)
        surface
            .contentShape([.interaction, .hoverEffect], RoundedRectangle(cornerRadius: 16, style: .continuous))
            .onTapGesture { if enabled { action() } }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(accessibilityLabel)
            .accessibilityAddTraits(.isButton)
            .accessibilityAction { if enabled { action() } }
            .disabled(!enabled)
            .hoverEffect { effect, active, _ in
                effect.scaleEffect(active && enabled && !reduceMotion ? 1.025 : 1)
            }
        #else
        Button(action: action) { surface }
            .buttonStyle(.plain)
            .disabled(!enabled)
            .accessibilityLabel(accessibilityLabel)
        #endif
    }

    private var surface: some View {
        VStack(alignment: .leading, spacing: 8) {
            EngineLibraryCover().frame(width: coverWidth, height: coverWidth * 1.5)
            VStack(alignment: .leading, spacing: 4) {
                Text("Halo: Combat Evolved")
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(.white)
                    .lineLimit(2)
                status()
                    .font(.caption.weight(.medium))
                    .foregroundStyle(EngineLibraryTheme.success)
            }
            .padding(.horizontal, 4)
            .frame(maxWidth: .infinity, minHeight: 40, alignment: .leading)
        }
        .padding(.horizontal, 6)
        .padding(.top, 6)
        .padding(.bottom, 16)
        .frame(width: coverWidth + 12)
        .background(EngineLibraryTheme.surface.opacity(reduceTransparency ? 1 : 0.9),
                    in: RoundedRectangle(cornerRadius: 16, style: .continuous))
        .overlay {
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .strokeBorder(EngineLibraryTheme.accent.opacity(0.42), lineWidth: 1)
        }
        .shadow(color: .black.opacity(0.35), radius: 24, y: 14)
    }
}
