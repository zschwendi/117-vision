import SwiftUI
import UIKit

/// Observe the real window attachment before dismissing the immersive scene.
/// This view owns neither the engine nor its frame presenter.
struct EngineMainWindowAttachment: UIViewRepresentable {
    let session: EngineAppSession

    func makeUIView(context: Context) -> Observer {
        let view = Observer()
        let id = view.observerID
        view.changed = { [weak session] window in session?.mainWindowChanged(window, observer: id) }
        return view
    }
    func updateUIView(_ view: Observer, context: Context) {}

    final class Observer: UIView {
        let observerID = UUID()
        var changed: ((UIWindow?) -> Void)?
        override func didMoveToWindow() {
            super.didMoveToWindow()
            changed?(window)
        }
    }
}
