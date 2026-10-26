import SwiftUI

struct EngineGraphicsModsView: View {
    @AppStorage("HaloTextureModsEnabled") private var textures = true
    @AppStorage("HaloShaderModsEnabled") private var shaders = true
    private var hasTextures: Bool { Bundle.main.url(forResource: "TextureMods", withExtension: "hvt") != nil }
    private var hasShaders: Bool { Bundle.main.url(forResource: "ShaderMods", withExtension: "hvs") != nil }

    var body: some View {
        List {
            Section {
                Label("Your visual packs", systemImage: "sparkles")
                    .font(.title2.bold())
                Text("Choose the original look or the visual packs included with this app. Changes apply after you fully close and reopen Master Cheef.")
                    .foregroundStyle(.secondary)
            }
            Section {
                Toggle("HD textures", isOn: $textures).disabled(!hasTextures)
                Text(hasTextures ? "The installed pack combines remastered textures with faithful upscales. Turn it off for Halo's original textures." : "No texture pack is included in this build.")
                    .font(.footnote).foregroundStyle(.secondary)
                Toggle("CEnshine shader fixes", isOn: $shaders).disabled(!hasShaders)
                Text(hasShaders ? "The included retail-adapted shader pack restores selected Xbox-style effects. Turn it off to use the original PC shaders." : "No shader pack is included in this build.")
                    .font(.footnote).foregroundStyle(.secondary)
            } header: { Text("Included · next app launch") }
            Section("More possibilities") {
                Label("Custom texture packs", systemImage: "photo.stack")
                Text("Compatible TexMod inputs can be converted with this project's texture tools. A new pack needs conversion and a visual check before it is bundled.")
                Label("More shader restorations", systemImage: "sun.max")
                Text("Additional CEnshine effects are candidates for adaptation. Only the shader subset packaged here is currently supported.")
                Label("Clarity and performance", systemImage: "display")
                Text("Try render resolution in Settings before adding heavier effects. Higher resolution uses more GPU time and memory.")
            }
            Section {
                Link("CEnshine project", destination: URL(string: "https://github.com/Sledmine/censhine")!)
                Text("Windows injectors such as ReShade and Chimera do not run directly in this native Apple build. Their effects would need a separate port.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
        }
        .navigationTitle("Graphics Mods")
    }
}
