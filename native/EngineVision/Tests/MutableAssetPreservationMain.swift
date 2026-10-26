import Foundation

@main struct MutableAssetPreservationMain {
    static func write(_ text: String, to url: URL) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try Data(text.utf8).write(to: url)
    }

    static func rejects(_ name: String, _ body: () throws -> Void) {
        do { try body(); fatalError("Expected rejection: \(name)") }
        catch { print("REJECT_PASS \(name)") }
    }

    static func assertContents(_ expected: String, at url: URL) throws {
        let actual = try Data(contentsOf: url)
        precondition(actual == Data(expected.utf8))
    }

    static func main() throws {
        let manager = FileManager.default
        let temporary = manager.temporaryDirectory.appending(path: "Halo-mutable-assets-\(UUID().uuidString)",
                                                               directoryHint: .isDirectory)
        let installed = temporary.appending(path: "installed", directoryHint: .isDirectory)
        let staging = temporary.appending(path: "staging", directoryHint: .isDirectory)
        defer { try? manager.removeItem(at: temporary) }

        let immutablePath = "maps/ui.map"
        let registryPath = EngineAssetImporter.mutableRegistrationPath
        let registry = installed.appending(path: registryPath)
        let defaultRegistry = String(repeating: "D", count: 1_202)
        let changedRegistry = String(repeating: "R", count: 1_257)
        let savePath = EngineAssetImporter.bundledMutableRoot + "/savegame.bin"
        let profilePath = EngineAssetImporter.bundledMutableRoot + "/saved/player_profiles/new/00.sav"
        let files = [
            EngineBundledManifest.File(path: immutablePath, bytes: 4, sha256: String(repeating: "0", count: 64)),
            EngineBundledManifest.File(path: registryPath, bytes: 1_202, sha256: String(repeating: "0", count: 64)),
            EngineBundledManifest.File(path: savePath, bytes: 7, sha256: String(repeating: "0", count: 64))
        ]

        try write("map!", to: installed.appending(path: immutablePath))
        try write(changedRegistry, to: registry)
        let changedSave = "player checkpoint changed size"
        try write(changedSave, to: installed.appending(path: savePath))
        try write("new profile progress", to: installed.appending(path: profilePath))

        // A changed writable file must not invalidate an otherwise complete
        // installed payload, even when its size differs from the manifest.
        try EngineAssetImporter.validatePreparedFiles(installed, files: files)
        print("MUTATED_SAVE_REUSE_PASS")
        print("MUTATED_REGISTRY_REUSE_PASS")

        // Registration is mutable, but only as the expected regular file.
        // Neither a missing file, a truncated export, nor a link is valid.
        try manager.removeItem(at: registry)
        rejects("missing registration") { try EngineAssetImporter.validatePreparedFiles(installed, files: files) }
        try write("short", to: registry)
        rejects("truncated registration") { try EngineAssetImporter.validatePreparedFiles(installed, files: files) }
        try manager.removeItem(at: registry)
        try manager.createDirectory(at: registry, withIntermediateDirectories: true)
        rejects("registration directory") { try EngineAssetImporter.validatePreparedFiles(installed, files: files) }
        try manager.removeItem(at: registry)
        let externalRegistry = temporary.appending(path: "external-registration")
        try write(changedRegistry, to: externalRegistry)
        try manager.createSymbolicLink(at: registry, withDestinationURL: externalRegistry)
        rejects("registration symlink") { try EngineAssetImporter.validatePreparedFiles(installed, files: files) }
        try write(defaultRegistry, to: staging.appending(path: registryPath))
        rejects("registration symlink repair") {
            try EngineAssetImporter.preserveMutableGameData(from: installed, into: staging)
        }
        try manager.removeItem(at: externalRegistry)
        rejects("dangling registration symlink repair") {
            try EngineAssetImporter.preserveMutableGameData(from: installed, into: staging)
        }
        try assertContents(defaultRegistry, at: staging.appending(path: registryPath))
        try manager.removeItem(at: registry)
        try write(changedRegistry, to: registry)

        try write("wrong immutable size", to: installed.appending(path: immutablePath))
        rejects("changed immutable size") { try EngineAssetImporter.validatePreparedFiles(installed, files: files) }
        try write("map!", to: installed.appending(path: immutablePath))

        try manager.removeItem(at: installed.appending(path: immutablePath))
        do {
            try EngineAssetImporter.validatePreparedFiles(installed, files: files)
            fatalError("missing immutable file was accepted")
        } catch {
            print("IMMUTABLE_MISSING_REPAIR_REQUIRED_PASS")
        }

        // Model the staged defaults created by importFolder before its repair
        // overlay, then prove both changed and newly-created progress survive.
        try write("map!", to: staging.appending(path: immutablePath))
        try write("default", to: staging.appending(path: savePath))
        try EngineAssetImporter.preserveMutableGameData(from: installed, into: staging)
        let preservedSave = String(decoding: try Data(contentsOf: staging.appending(path: savePath)), as: UTF8.self)
        let preservedProfile = String(decoding: try Data(contentsOf: staging.appending(path: profilePath)), as: UTF8.self)
        precondition(preservedSave == changedSave)
        precondition(preservedProfile == "new profile progress")
        try assertContents(changedRegistry, at: staging.appending(path: registryPath))
        try assertContents(changedRegistry, at: registry)
        print("REPAIR_PRESERVES_SAVE_TREE_PASS")
        print("REPAIR_PRESERVES_REGISTRY_PASS")

        // A player who has not created a profile still owns their registration.
        let registryOnly = temporary.appending(path: "registry-only")
        try write(changedRegistry, to: registryOnly.appending(path: registryPath))
        try write(defaultRegistry, to: staging.appending(path: registryPath))
        try EngineAssetImporter.preserveMutableGameData(from: registryOnly, into: staging)
        try assertContents(changedRegistry, at: staging.appending(path: registryPath))
        print("REGISTRY_WITHOUT_SAVE_TREE_PRESERVED_PASS")

        let freshInstall = temporary.appending(path: "not-yet-installed")
        try write(defaultRegistry, to: staging.appending(path: registryPath))
        try EngineAssetImporter.preserveMutableGameData(from: freshInstall, into: staging)
        try assertContents(defaultRegistry, at: staging.appending(path: registryPath))
        print("FRESH_INSTALL_REGISTRY_DEFAULT_PRESERVED_PASS")

        let promotionDestination = temporary.appending(path: "promotion-destination", directoryHint: .isDirectory)
        let originalSave = promotionDestination.appending(path: savePath)
        try write("irreplaceable progress", to: originalSave)
        let missingStaging = temporary.appending(path: "missing-staging", directoryHint: .isDirectory)
        do {
            try EngineAssetImporter.promoteStaging(missingStaging, to: promotionDestination)
            fatalError("missing staging directory was promoted")
        } catch {
            let afterFailure = String(decoding: try Data(contentsOf: originalSave), as: UTF8.self)
            precondition(afterFailure == "irreplaceable progress")
            print("PROMOTION_FAILURE_ROLLBACK_PASS")
        }
    }
}
