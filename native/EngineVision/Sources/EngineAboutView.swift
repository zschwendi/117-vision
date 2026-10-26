import SwiftUI

struct EngineAboutView: View {
    private var version: String {
        let info = Bundle.main.infoDictionary ?? [:]
        return "Version \(info["CFBundleShortVersionString"] as? String ?? "—") · Build \(info["CFBundleVersion"] as? String ?? "—")"
    }

    var body: some View {
        List {
            Section {
                VStack(alignment: .leading, spacing: 10) {
                    Label(EngineLibraryTheme.productName, systemImage: "sparkles.tv").font(.title2.bold())
                    Text("Halo: Combat Evolved, at home on your Apple devices.")
                    Text(version).font(.footnote).foregroundStyle(.secondary)
                }.padding(.vertical, 8)
            }
            Section("Credits") {
                Text("Halo by Bungie · PC port by Gearbox · Published by Microsoft")
                Text("Based on Master Chef by mitchaiet and contributors.")
                Text("Texture pack: Delta117 and the Master Chef visual pack contributors.")
                Text("Adapted CEnshine shaders: Sledmine and contributors.")
            }
            Section("Project") {
                Link("\(EngineLibraryTheme.productName) source", destination: URL(string: "https://github.com/zschwendi/117-vision")!)
                Link("Master Chef foundation", destination: URL(string: "https://github.com/mitchaiet/master-chef")!)
                Link("CEnshine shaders", destination: URL(string: "https://github.com/Sledmine/censhine")!)
                Text("An unofficial community port.").foregroundStyle(.secondary)
            }
            Section("Third-party notices") {
                Text(notices).font(.footnote).textSelection(.enabled)
            }
        }
        .navigationTitle("About \(EngineLibraryTheme.productName)")
    }

    private var notices: String {
        guard let url = Bundle.main.url(forResource: "ThirdPartyNotices", withExtension: "txt"),
              let value = try? String(contentsOf: url, encoding: .utf8) else {
            return "See the project source for licenses and acknowledgments."
        }
        return value
    }
}
