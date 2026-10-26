import SwiftUI
import UIKit

/// Presents the game full screen from a UIKit container that can express
/// pointer lock: SwiftUI's fullScreenCover hosting controller cannot, and the
/// system reads prefersPointerLocked from the presented full-screen
/// controller. The SwiftUI game content (frame view, input surface, overlay,
/// settings sheet) is hosted unchanged as a child.
struct EnginePadGamePresenter<Content: View>: UIViewControllerRepresentable {
    let isPresented: Bool
    let input: EnginePadInputRouter
    @ViewBuilder let content: () -> Content

    func makeUIViewController(context: Context) -> Anchor { Anchor() }
    func updateUIViewController(_ anchor: Anchor, context: Context) {
        anchor.update(isPresented: isPresented, input: input, content: AnyView(content()))
    }
    static func dismantleUIViewController(_ anchor: Anchor, coordinator: ()) { anchor.dismissGame() }

    /// Zero-size placeholder in the Library hierarchy that owns the presentation.
    /// EnginePadPresentationState serializes present/dismiss transitions.
    final class Anchor: UIViewController {
        private var state = EnginePadPresentationState()
        private var game: EnginePadGameViewController?
        private var input: EnginePadInputRouter?
        private var content: AnyView?

        override func loadView() { view = UIView(); view.isUserInteractionEnabled = false; view.isHidden = true }
        override func viewDidAppear(_ animated: Bool) {
            super.viewDidAppear(animated)
            perform(state.poke(canPresent: canPresent))
        }

        func update(isPresented: Bool, input: EnginePadInputRouter, content: AnyView) {
            self.input = input; self.content = content
            game?.setContent(content)
            perform(state.desire(isPresented, canPresent: canPresent))
        }
        func dismissGame() { perform(state.desire(false, canPresent: false)) }

        private var canPresent: Bool { view.window != nil && input != nil && content != nil }

        private func perform(_ action: EnginePadPresentationState.Action) {
            switch action {
            case .none: break
            case .present:
                guard let input, let content else { state.presentRefused(); return }
                var presenter: UIViewController = self
                while let next = presenter.presentedViewController, !next.isBeingDismissed { presenter = next }
                let controller = EnginePadGameViewController(content: content, input: input)
                game = controller
                presenter.present(controller, animated: true) { [weak self, weak controller] in
                    // Only the accepted presentation completes the transition.
                    guard let self, let controller, self.game === controller else { return }
                    self.transitionFinished()
                }
                if controller.presentingViewController == nil {   // refused (already presenting, detached)
                    game = nil; state.presentRefused()
                }
            case .dismiss:
                guard let game, let presenting = game.presentingViewController else {
                    self.game = nil; transitionFinished(); return
                }
                presenting.dismiss(animated: true) { [weak self] in
                    guard let self else { return }
                    if self.game === game { self.game = nil }
                    self.transitionFinished()
                }
            }
        }
        private func transitionFinished() { perform(state.finished(canPresent: canPresent)) }
    }
}

final class EnginePadGameViewController: UIViewController {
    private let hosting: UIHostingController<AnyView>
    private let input: EnginePadInputRouter

    init(content: AnyView, input: EnginePadInputRouter) {
        hosting = UIHostingController(rootView: content)
        self.input = input
        super.init(nibName: nil, bundle: nil)
        modalPresentationStyle = .fullScreen
        isModalInPresentation = true
    }
    required init?(coder: NSCoder) { nil }

    func setContent(_ content: AnyView) { hosting.rootView = content }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .black
        addChild(hosting)
        hosting.view.frame = view.bounds
        hosting.view.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        hosting.view.backgroundColor = .black
        view.addSubview(hosting.view)
        hosting.didMove(toParent: self)
    }
    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        input.gameControllerAppeared(self)
        setNeedsUpdateOfPrefersPointerLocked()
    }
    override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear(animated)
        input.gameControllerDisappearing(self)
    }

    /// A request only; the router follows the resolved UIPointerLockState.
    override var prefersPointerLocked: Bool { input.prefersPointerLock }
    override var childViewControllerForPointerLock: UIViewController? { nil }
    // SwiftUI's .persistentSystemOverlays(.hidden) lives on the hosted child.
    override var childForHomeIndicatorAutoHidden: UIViewController? { hosting }
    override var childForScreenEdgesDeferringSystemGestures: UIViewController? { hosting }
    override var childForStatusBarHidden: UIViewController? { hosting }
    override var childForStatusBarStyle: UIViewController? { hosting }
}
