import Foundation
import CryptoKit
import Darwin

/// A bounded, self-contained save transfer. It never contains the executable,
/// registration, maps, renderer caches, or files outside Halo's writable tree.
struct EngineSaveArchive: Codable, Sendable {
    struct Entry: Codable, Sendable {
        let path: String
        let data: Data
        let sha256: String
    }
    static let gameIdentifier = "halo-pc-1.10"
    static let mutablePath = "Users/Player/Documents/My Games/Halo"
    static let fileExtension = "mc-save"
    static let maximumBytes = 64 * 1024 * 1024
    static let maximumEncodedBytes = 80 * 1024 * 1024
    static let maximumFiles = 2048
    let version: Int
    let game: String
    let id: UUID
    let created: Date
    let device: String
    let entries: [Entry]
    /// Version two keeps immutable ancestry. Content hashes identify bytes;
    /// snapshot IDs identify revisions, including an explicit conflict choice.
    var sourcePlatform: String? = nil
    var parentFingerprint: String? = nil
    var parentSnapshotIDs: [UUID]? = nil
    var lineageID: UUID? = nil

    var isVisionProSource: Bool {
        if version >= 2 { return sourcePlatform == "Vision Pro" }
        return device == "Vision Pro" || device.hasPrefix("Vision Pro · ")
    }

    /// `platform` is the validated wire family; `device` is only a display label
    /// (an iPhone publishes as the "iPad" family so existing readers accept it).
    func revision(platform: String, parents: [Self], lineage: UUID?, device: String? = nil) -> Self {
        var result = Self(version: 2, game: game, id: UUID(), created: .now,
                          device: String((device ?? platform).prefix(80)), entries: entries)
        result.sourcePlatform = platform
        result.parentFingerprint = parents.first?.fingerprint
        result.parentSnapshotIDs = parents.map(\.id)
        result.lineageID = lineage ?? lineageID ?? id
        return result
    }

    var totalBytes: Int { entries.reduce(0) { $0 + $1.data.count } }
    var fingerprint: String {
        var hash = SHA256()
        for entry in entries.sorted(by: { $0.path < $1.path }) {
            hash.update(data: Data(entry.path.utf8)); hash.update(data: Data([0]))
            hash.update(data: Data(entry.sha256.utf8)); hash.update(data: Data([0]))
        }
        return hash.finalize().map { String(format: "%02x", $0) }.joined()
    }
    var filename: String { "\(id.uuidString.lowercased()).\(Self.fileExtension)" }

    static func mutableRoot(gameRoot: URL) throws -> URL {
        // Resolve only the trusted container root, then reject links anywhere
        // beneath it, including an otherwise tempting linked save directory.
        var cursor = gameRoot.resolvingSymlinksInPath()
        try requireDirectory(cursor)
        for component in mutablePath.split(separator: "/") {
            cursor.appendPathComponent(String(component), isDirectory: true)
            if (try? FileManager.default.attributesOfItem(atPath: cursor.path)) != nil { try requireDirectory(cursor) }
        }
        return cursor
    }

    static func capture(gameRoot: URL, device: String, engineActive: Bool) throws -> Self {
        guard !engineActive else { throw EngineSaveError.engineActive }
        let root = try mutableRoot(gameRoot: gameRoot)
        try requireDirectory(root)
        let manager = FileManager.default
        var files: [Entry] = [], total = 0
        var scanError: Error?
        guard let enumerator = manager.enumerator(at: root, includingPropertiesForKeys: nil,
            options: [], errorHandler: { _, error in scanError = error; return false }) else {
            throw EngineSaveError.invalid("The local save folder cannot be read.")
        }
        for case let url as URL in enumerator {
            let attributes = try manager.attributesOfItem(atPath: url.path)
            let type = attributes[.type] as? FileAttributeType
            if type == .typeDirectory { continue }
            guard type == .typeRegular, (attributes[.referenceCount] as? NSNumber)?.intValue ?? 1 <= 1 else {
                throw EngineSaveError.invalid("Linked or special files cannot be synced.")
            }
            // Directory enumeration may expand /var to /private/var even
            // when the input URL does not. Compare canonical components,
            // never subtract an unverified string prefix.
            let rootComponents = root.resolvingSymlinksInPath().pathComponents
            let fileComponents = url.resolvingSymlinksInPath().pathComponents
            guard fileComponents.starts(with: rootComponents), fileComponents.count > rootComponents.count else {
                throw EngineSaveError.invalid("A save path escaped its root.")
            }
            let path = fileComponents.dropFirst(rootComponents.count).joined(separator: "/")
            try validatePath(path)
            let bytes = (attributes[.size] as? NSNumber)?.intValue ?? -1
            guard bytes >= 0, bytes <= maximumBytes, total <= maximumBytes - bytes,
                  files.count < maximumFiles else { throw EngineSaveError.tooLarge }
            let data = try Data(contentsOf: url)
            guard data.count == bytes else { throw EngineSaveError.changed }
            total += data.count
            files.append(Entry(path: path, data: data, sha256: digest(data)))
        }
        if let scanError { throw scanError }
        guard !files.isEmpty else { throw EngineSaveError.empty }
        let result = Self(version: 1, game: gameIdentifier, id: UUID(), created: .now,
                          device: String(device.prefix(80)), entries: files.sorted { $0.path < $1.path })
        try result.validate()
        return result
    }

    func validate() throws {
        guard (version == 1 || version == 2), game == Self.gameIdentifier, !entries.isEmpty,
              entries.count <= Self.maximumFiles, device.utf8.count <= 320,
              created.timeIntervalSince1970.isFinite else {
            throw EngineSaveError.invalid("This is not a compatible save snapshot.")
        }
        if version == 2 {
            guard let sourcePlatform, ["Vision Pro", "iPad", "Mac"].contains(sourcePlatform),
                  let parents = parentSnapshotIDs, parents.count <= 500,
                  Set(parents).count == parents.count, !parents.contains(id),
                  lineageID != nil,
                  (parents.isEmpty && parentFingerprint == nil)
                    || (!parents.isEmpty && parentFingerprint?.count == 64
                        && parentFingerprint!.allSatisfy({ $0.isHexDigit })) else {
                throw EngineSaveError.invalid("This snapshot has invalid revision ancestry.")
            }
        } else if sourcePlatform != nil || parentFingerprint != nil
                    || parentSnapshotIDs != nil || lineageID != nil {
            throw EngineSaveError.invalid("A legacy snapshot cannot contain revision ancestry.")
        }
        var names = Set<String>(), total = 0
        for entry in entries {
            try Self.validatePath(entry.path)
            guard names.insert(entry.path.precomposedStringWithCanonicalMapping.lowercased()).inserted else {
                throw EngineSaveError.invalid("The snapshot contains duplicate save paths.")
            }
            guard entry.data.count <= Self.maximumBytes, total <= Self.maximumBytes - entry.data.count else {
                throw EngineSaveError.tooLarge
            }
            total += entry.data.count
            guard entry.sha256 == Self.digest(entry.data) else { throw EngineSaveError.invalid("A save checksum does not match.") }
        }
        // Reject file/directory collisions even on a case-sensitive sender.
        for name in names {
            var parts = name.split(separator: "/").map(String.init)
            while parts.count > 1 {
                parts.removeLast()
                guard !names.contains(parts.joined(separator: "/")) else {
                    throw EngineSaveError.invalid("The snapshot contains conflicting paths.")
                }
            }
        }
    }

    func encoded() throws -> Data {
        try validate()
        let encoder = PropertyListEncoder(); encoder.outputFormat = .binary
        let data = try encoder.encode(self)
        guard data.count <= Self.maximumEncodedBytes else { throw EngineSaveError.tooLarge }
        return data
    }

    static func read(_ url: URL) throws -> Self {
        let attrs = try FileManager.default.attributesOfItem(atPath: url.path)
        guard attrs[.type] as? FileAttributeType == .typeRegular else {
            throw EngineSaveError.invalid("A save snapshot must be a regular file.")
        }
        let size = (attrs[.size] as? NSNumber)?.intValue ?? -1
        guard size > 0, size <= maximumEncodedBytes else { throw EngineSaveError.tooLarge }
        let data = try Data(contentsOf: url)
        guard data.count <= maximumEncodedBytes else { throw EngineSaveError.tooLarge }
        let archive = try PropertyListDecoder().decode(Self.self, from: data)
        try archive.validate()
        return archive
    }

    /// No existing snapshot is replaced. A caller coordinates external writes.
    func writeNew(in folder: URL) throws -> URL {
        try Self.requireDirectory(folder)
        let target = folder.appendingPathComponent(filename)
        // .withoutOverwriting is intentional: snapshots are immutable, never a
        // shared "latest" file that can win a race against another device.
        try encoded().write(to: target, options: [.withoutOverwriting])
        return target
    }

    /// Explicit restore only. First persists an independent recovery snapshot,
    /// then stages and swaps the complete save tree atomically on Apple OSes.
    /// If process termination follows the swap, the prior tree also remains at
    /// the staging path until normal cleanup; it is never silently discarded.
    @discardableResult
    func restore(gameRoot: URL, recoveryFolder: URL, engineActive: Bool) throws -> URL? {
        guard !engineActive else { throw EngineSaveError.engineActive }
        try validate()
        let manager = FileManager.default
        let target = try Self.mutableRoot(gameRoot: gameRoot)
        let existed = manager.fileExists(atPath: target.path)
        var prior: Self?
        if existed {
            do { prior = try Self.capture(gameRoot: gameRoot, device: "Before restore", engineActive: false) }
            catch EngineSaveError.empty { /* An empty directory has no progress to preserve. */ }
        }
        try manager.createDirectory(at: recoveryFolder, withIntermediateDirectories: true)
        let recoveryURL = try prior?.writeNew(in: recoveryFolder)
        let parent = target.deletingLastPathComponent()
        try manager.createDirectory(at: parent, withIntermediateDirectories: true)
        let staging = parent.appendingPathComponent(".MasterCheefRestore-\(UUID().uuidString)", isDirectory: true)
        try manager.createDirectory(at: staging, withIntermediateDirectories: false)
        var swapped = false
        defer { if !swapped { try? manager.removeItem(at: staging) } }
        for entry in entries {
            let destination = staging.appendingPathComponent(entry.path)
            try manager.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
            try entry.data.write(to: destination, options: [.withoutOverwriting])
        }
        if existed {
            // A second capture refuses an unexpected local change during work.
            var current: Self?
            do { current = try Self.capture(gameRoot: gameRoot, device: "Check", engineActive: false) }
            catch EngineSaveError.empty { }
            guard current?.fingerprint == prior?.fingerprint else { throw EngineSaveError.changed }
            try Self.requireDirectory(target)
            let result = staging.path.withCString { from in
                target.path.withCString { to in renamex_np(from, to, UInt32(RENAME_SWAP)) }
            }
            guard result == 0 else { throw NSError(domain: NSPOSIXErrorDomain, code: Int(errno)) }
            swapped = true
            // Existing progress is already kept in the recovery archive. If
            // cleanup fails, leave the second copy in its private staging path.
            try? manager.removeItem(at: staging)
        } else {
            try manager.moveItem(at: staging, to: target)
            swapped = true
        }
        return recoveryURL
    }

    static func digest(_ data: Data) -> String { SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined() }
    private static func validatePath(_ path: String) throws {
        let components = path.split(separator: "/", omittingEmptySubsequences: false)
        guard path.utf8.count <= 1024, !components.isEmpty,
              !path.contains("\\"), !path.contains(":"),
              !path.unicodeScalars.contains(where: { $0.value < 32 || $0.value == 127 }),
              !components.contains(where: { $0.isEmpty || $0 == "." || $0 == ".." }) else {
            throw EngineSaveError.invalid("The snapshot contains an unsafe save path.")
        }
    }
    private static func requireDirectory(_ url: URL) throws {
        let attrs = try FileManager.default.attributesOfItem(atPath: url.path)
        guard attrs[.type] as? FileAttributeType == .typeDirectory else {
            throw EngineSaveError.invalid("Save folders must be real directories, not links.")
        }
    }
}

enum EngineSaveError: LocalizedError {
    case engineActive, empty, changed, tooLarge, invalid(String)
    var errorDescription: String? {
        switch self {
        case .engineActive: return "Close and reopen the app, then sync before pressing Play. Returning to the launcher does not stop Halo from writing saves."
        case .empty: return "There are no local profiles or saves to back up yet."
        case .changed: return "Local saves changed during the operation. Nothing was restored; try again with the game stopped."
        case .tooLarge: return "This snapshot exceeds the save limit (64 MB or 2,048 files). No local saves were replaced."
        case .invalid(let message): return message
        }
    }
}
