import Foundation
import ImageIO
import UniformTypeIdentifiers

/// Player-selected launcher images live beside app preferences, never in the
/// game/save tree or iCloud. Only a bounded, orientation-correct thumbnail is
/// retained; metadata and any additional animation frames are discarded.
struct EngineLibraryArtworkStorage: Sendable {
    static let maximumFileBytes = 25 * 1_024 * 1_024
    static let maximumPixelCount = 50_000_000
    static let thumbnailDimension = 1_600
    let directory: URL
    var artworkURL: URL { directory.appendingPathComponent("halo-ce.png") }
    var hasArtwork: Bool { FileManager.default.fileExists(atPath: artworkURL.path) }

    enum ArtworkError: LocalizedError {
        case invalidFile, fileTooLarge, invalidImage, imageTooLarge, encodingFailed
        var errorDescription: String? {
            switch self {
            case .invalidFile: return "Choose an image file. Folders and file shortcuts cannot be used as artwork."
            case .fileTooLarge: return "Choose an image smaller than 25 MB."
            case .invalidImage: return "This file could not be opened as an image. Try a PNG, JPEG, or HEIC image."
            case .imageTooLarge: return "Choose an image with no more than 50 megapixels."
            case .encodingFailed: return "The artwork could not be prepared. Try another image."
            }
        }
    }

    static var defaultDirectory: URL {
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Library/Application Support")
        return support.appendingPathComponent("117/LibraryArtwork", isDirectory: true)
    }

    func importArtwork(from source: URL) throws -> Data {
        let scoped = source.startAccessingSecurityScopedResource()
        defer { if scoped { source.stopAccessingSecurityScopedResource() } }
        var coordinateError: NSError?
        var result: Result<Data, Error>?
        NSFileCoordinator().coordinate(readingItemAt: source, options: .withoutChanges, error: &coordinateError) { readableURL in
            result = Result { try Self.prepareImage(Self.readBoundedFile(readableURL)) }
        }
        if let coordinateError { throw coordinateError }
        guard let result else { throw ArtworkError.invalidFile }
        let prepared = try result.get()
        try Task.checkCancellation()
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        // A failed import cannot replace the previous image with partial bytes.
        try prepared.write(to: artworkURL, options: .atomic)
        return prepared
    }

    func loadArtwork() throws -> Data? {
        guard hasArtwork else { return nil }
        return try Self.prepareImage(Self.readBoundedFile(artworkURL))
    }

    func removeArtwork() throws {
        guard hasArtwork else { return }
        try FileManager.default.removeItem(at: artworkURL)
    }

    private static func readBoundedFile(_ url: URL) throws -> Data {
        let values = try url.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey])
        guard values.isRegularFile == true, values.isSymbolicLink != true else { throw ArtworkError.invalidFile }
        guard let size = values.fileSize, size > 0 else { throw ArtworkError.invalidImage }
        guard size <= maximumFileBytes else { throw ArtworkError.fileTooLarge }
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        var bytes = Data()
        // Check actual bytes as well as file metadata (a provider/file may
        // change between stat and reading). Never map an unbounded source.
        while let chunk = try handle.read(upToCount: 64 * 1_024), !chunk.isEmpty {
            guard chunk.count <= maximumFileBytes - bytes.count else { throw ArtworkError.fileTooLarge }
            bytes.append(chunk)
        }
        return bytes
    }

    static func prepareImage(_ data: Data) throws -> Data {
        guard !data.isEmpty else { throw ArtworkError.invalidImage }
        guard data.count <= maximumFileBytes else { throw ArtworkError.fileTooLarge }
        guard let source = CGImageSourceCreateWithData(data as CFData, [kCGImageSourceShouldCache: false] as CFDictionary),
              CGImageSourceGetStatus(source) == .statusComplete,
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              let width = properties[kCGImagePropertyPixelWidth] as? NSNumber,
              let height = properties[kCGImagePropertyPixelHeight] as? NSNumber else { throw ArtworkError.invalidImage }
        let w = width.doubleValue, h = height.doubleValue
        guard w.isFinite, h.isFinite, w >= 1, h >= 1,
              w <= 32_768, h <= 32_768, w * h <= Double(maximumPixelCount) else { throw ArtworkError.imageTooLarge }
        let options: [CFString: Any] = [kCGImageSourceCreateThumbnailFromImageAlways: true,
                                      kCGImageSourceCreateThumbnailWithTransform: true,
                                      kCGImageSourceThumbnailMaxPixelSize: thumbnailDimension,
                                      kCGImageSourceShouldCacheImmediately: true]
        guard let thumbnail = CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary) else {
            throw ArtworkError.invalidImage
        }
        let output = NSMutableData()
        guard let destination = CGImageDestinationCreateWithData(output, UTType.png.identifier as CFString, 1, nil) else {
            throw ArtworkError.encodingFailed
        }
        CGImageDestinationAddImage(destination, thumbnail, nil)
        guard CGImageDestinationFinalize(destination), output.length <= maximumFileBytes else { throw ArtworkError.encodingFailed }
        return output as Data
    }
}

#if canImport(UIKit)
import SwiftUI
import UIKit

@MainActor
final class EngineLibraryArtworkStore: ObservableObject {
    static let shared = EngineLibraryArtworkStore()
    @Published private(set) var image: UIImage?
    @Published private(set) var hasCustomArtwork: Bool
    @Published private(set) var isBusy = false
    @Published var isImporterPresented = false
    @Published var errorMessage: String?
    private let storage: EngineLibraryArtworkStorage

    init(directory: URL = EngineLibraryArtworkStorage.defaultDirectory) {
        storage = EngineLibraryArtworkStorage(directory: directory)
        hasCustomArtwork = storage.hasArtwork
        guard hasCustomArtwork else { return }
        isBusy = true
        Task {
            let stored = await Task.detached(priority: .userInitiated) { [storage] in try? storage.loadArtwork() }.value
            image = stored.flatMap { UIImage(data: $0) }
            isBusy = false
        }
    }

    func chooseArtwork() { if !isBusy { isImporterPresented = true } }

    func importArtwork(from url: URL) async {
        guard !isBusy else { return }
        isBusy = true
        defer { isBusy = false }
        do {
            let prepared = try await Task.detached(priority: .userInitiated) { [storage] in
                try storage.importArtwork(from: url)
            }.value
            image = UIImage(data: prepared)
            hasCustomArtwork = true
        } catch { errorMessage = error.localizedDescription }
    }

    func useDefaultArtwork() {
        guard !isBusy else { return }
        do {
            try storage.removeArtwork()
            image = nil
            hasCustomArtwork = false
        } catch { errorMessage = error.localizedDescription }
    }
}

struct EngineLibraryArtworkActions: View {
    @ObservedObject private var artwork = EngineLibraryArtworkStore.shared
    var body: some View {
        Button("Change Artwork…", systemImage: "photo") { artwork.chooseArtwork() }
            .disabled(artwork.isBusy)
        if artwork.hasCustomArtwork {
            Button("Use Default Artwork", systemImage: "arrow.uturn.backward", role: .destructive) { artwork.useDefaultArtwork() }
                .disabled(artwork.isBusy)
        }
    }
}

struct EngineLibraryArtworkMenu: View {
    @ObservedObject private var artwork = EngineLibraryArtworkStore.shared
    var body: some View {
        Menu { EngineLibraryArtworkActions() } label: {
            Group {
                if artwork.isBusy { ProgressView().controlSize(.small) }
                else { Image(systemName: "ellipsis").font(.headline) }
            }
            .foregroundStyle(.white)
            .frame(width: 36, height: 36)
            .background(.black.opacity(0.65), in: Circle())
            .frame(width: 44, height: 44)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .disabled(artwork.isBusy)
        .accessibilityLabel("Artwork options for Halo: Combat Evolved")
        #if os(visionOS)
        .senseControllerTarget("library.artwork")
        #endif
    }
}

private struct EngineLibraryArtworkImporter: ViewModifier {
    @ObservedObject private var artwork = EngineLibraryArtworkStore.shared
    func body(content: Content) -> some View {
        content
            .background {
                // Keep this presentation host separate from the launcher's
                // existing game-folder importer and its error alert.
                Color.clear.frame(width: 0, height: 0)
                    .fileImporter(isPresented: $artwork.isImporterPresented, allowedContentTypes: [.image], allowsMultipleSelection: false) { result in
                        switch result {
                        case .success(let urls):
                            if let url = urls.first { Task { await artwork.importArtwork(from: url) } }
                        case .failure(let error):
                            if (error as NSError).code != NSUserCancelledError { artwork.errorMessage = error.localizedDescription }
                        }
                    }
                    .alert("Artwork could not be changed", isPresented: Binding(get: { artwork.errorMessage != nil }, set: { if !$0 { artwork.errorMessage = nil } })) {
                        Button("OK", role: .cancel) { artwork.errorMessage = nil }
                    } message: { Text(artwork.errorMessage ?? "") }
                }
    }
}

extension View {
    func libraryArtworkImporter() -> some View { modifier(EngineLibraryArtworkImporter()) }
}
#endif
