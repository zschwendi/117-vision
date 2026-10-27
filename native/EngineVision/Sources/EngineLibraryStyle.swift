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

/// Launcher-only atmosphere: a static deep blue-green sky, stars and an
/// original geometric ring band. Procedural; no game artwork is used.
struct EngineLibraryBackground: View {
    var body: some View {
        Canvas { context, size in
            Self.draw(in: &context, size: size)
        }
        .background(EngineLibraryTheme.background)
        .clipped()
        .allowsHitTesting(false)
        .accessibilityHidden(true)
    }

    private static func draw(in context: inout GraphicsContext, size: CGSize) {
        let extent = max(size.width, size.height, 1)
        let full = CGRect(origin: .zero, size: size)
        context.fill(Path(full), with: .color(EngineLibraryTheme.background))

        // Two soft color fields behind the shelf.
        context.fill(Path(full), with: .radialGradient(
            Gradient(colors: [Color(red: 0.05, green: 0.20, blue: 0.30).opacity(0.85), .clear]),
            center: CGPoint(x: size.width * 0.30, y: size.height * 0.28),
            startRadius: 0, endRadius: extent * 0.62))
        context.fill(Path(full), with: .radialGradient(
            Gradient(colors: [Color(red: 0.04, green: 0.24, blue: 0.17).opacity(0.70), .clear]),
            center: CGPoint(x: size.width * 0.74, y: size.height * 0.80),
            startRadius: 0, endRadius: extent * 0.55))

        // A deterministic, still star field.
        for index in 0..<96 {
            let x = CGFloat((index * 137 + 29) % 997) / 997 * size.width
            let y = CGFloat((index * 251 + 61) % 991) / 991 * size.height
            let radius: CGFloat = index.isMultiple(of: 9) ? 1.2 : 0.6
            let opacity = 0.10 + 0.16 * (1 + sin(Double(index) * 1.7)) / 2
            context.fill(Path(ellipseIn: CGRect(x: x - radius, y: y - radius, width: radius * 2, height: radius * 2)),
                         with: .color(.white.opacity(opacity)))
        }

        // An original ring: a tilted, foreshortened band seen edge-on, with a
        // lit inner surface. It does not animate while choosing a game.
        let center = CGPoint(x: size.width * 0.5, y: size.height * 0.56)
        let ringWidth = extent * 1.18
        let ringHeight = ringWidth * 0.17
        var band = context
        band.translateBy(x: center.x, y: center.y)
        band.rotate(by: .degrees(-9))
        let outer = CGRect(x: -ringWidth / 2, y: -ringHeight / 2, width: ringWidth, height: ringHeight)
        // Layered wide strokes give a soft glow.
        band.stroke(Path(ellipseIn: outer), with: .color(EngineLibraryTheme.accentGlow.opacity(0.035)), lineWidth: 30)
        band.stroke(Path(ellipseIn: outer), with: .color(EngineLibraryTheme.accent.opacity(0.06)), lineWidth: 12)
        let phase: CGFloat = 0.38
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

/// Plain, static label for the centered launcher menu.
struct EngineLibraryTitleLabel: View {
    var body: some View {
        HStack(spacing: 7) {
            Text(EngineLibraryTheme.productName)
                .font(.headline.weight(.semibold))
            Image(systemName: "chevron.down")
                .font(.caption.weight(.semibold))
        }
        .foregroundStyle(.primary)
        .padding(.horizontal, 16)
        .frame(minHeight: 44)
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
                    .allowsHitTesting(false)
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

    private var launchSurface: some View {
        Button(action: action) {
            surface
                .contentShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
        }
            .buttonStyle(.plain)
            .disabled(!enabled)
            .accessibilityLabel(accessibilityLabel)
            .accessibilityIdentifier("launcher.play")
        #if os(visionOS)
            .contentShape([.interaction, .hoverEffect], RoundedRectangle(cornerRadius: 16, style: .continuous))
            .hoverEffect(.highlight)
            .senseControllerTarget("library.play")
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
                    .allowsHitTesting(false)
        }
        .shadow(color: .black.opacity(0.35), radius: 24, y: 14)
    }
}
