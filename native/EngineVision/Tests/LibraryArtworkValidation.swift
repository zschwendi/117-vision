import Foundation
import ImageIO
import CoreGraphics
import UniformTypeIdentifiers

@main
enum LibraryArtworkValidation {
    static var checks = 0
    static func expect(_ value: @autoclosure () throws -> Bool, _ message: String) {
        checks += 1
        do { guard try value() else { fatalError(message) } }
        catch { fatalError("\(message): \(error)") }
    }
    static func expectFailure(_ message: String, _ operation: () throws -> Void) {
        do { try operation(); fatalError(message) } catch { checks += 1 }
    }
    static func image(width: Int, height: Int, type: UTType = .png, orientation: Int? = nil) -> Data {
        let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8,
                                bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        context.setFillColor(CGColor(red: 0.1, green: 0.6, blue: 0.9, alpha: 1))
        context.fill(CGRect(x: 0, y: 0, width: width, height: height))
        let output = NSMutableData()
        let destination = CGImageDestinationCreateWithData(output, type.identifier as CFString, 1, nil)!
        var properties: [CFString: Any] = [:]
        if let orientation { properties[kCGImagePropertyOrientation] = orientation }
        properties[kCGImagePropertyTIFFDictionary] = [kCGImagePropertyTIFFArtist: "Metadata must not be retained"]
        CGImageDestinationAddImage(destination, context.makeImage()!, properties as CFDictionary)
        precondition(CGImageDestinationFinalize(destination))
        return output as Data
    }
    static func dimensions(_ data: Data) -> (Int, Int) {
        let source = CGImageSourceCreateWithData(data as CFData, nil)!
        let values = CGImageSourceCopyPropertiesAtIndex(source, 0, nil)! as NSDictionary
        return ((values[kCGImagePropertyPixelWidth] as! NSNumber).intValue,
                (values[kCGImagePropertyPixelHeight] as! NSNumber).intValue)
    }
    static func main() {
        do { try run() }
        catch { fatalError("After \(checks) artwork checks: \(error)") }
    }
    static func run() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("117-artwork-tests-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let storage = EngineLibraryArtworkStorage(directory: root.appendingPathComponent("artwork"))
        expect(!storage.hasArtwork, "fresh storage should use default")
        expect(try storage.loadArtwork() == nil, "fresh storage must have no custom data")
        let input = root.appendingPathComponent("source.png")
        let bytes = image(width: 2400, height: 1200)
        try bytes.write(to: input)
        let imported = try storage.importArtwork(from: input)
        expect(dimensions(imported) == (1600, 800), "large images must downsample while retaining aspect ratio")
        expect(storage.hasArtwork, "successful import should persist")
        expect(try Data(contentsOf: input) == bytes, "import must not mutate the original")
        let afterRelaunch = EngineLibraryArtworkStorage(directory: storage.directory)
        expect(try afterRelaunch.loadArtwork() != nil, "new store must reopen persisted image")
        let persisted = try Data(contentsOf: storage.artworkURL)
        let imageSource = CGImageSourceCreateWithData(persisted as CFData, nil)!
        let properties = CGImageSourceCopyPropertiesAtIndex(imageSource, 0, nil)! as NSDictionary
        expect((properties[kCGImagePropertyTIFFDictionary] as? NSDictionary)?[kCGImagePropertyTIFFArtist] == nil,
               "image metadata should not be retained")
        let oriented = try EngineLibraryArtworkStorage.prepareImage(image(width: 320, height: 160, type: .jpeg, orientation: 6))
        expect(dimensions(oriented) == (160, 320), "EXIF orientation must be applied to pixels")
        let invalid = root.appendingPathComponent("not-an-image.png")
        try Data("not image content".utf8).write(to: invalid)
        expectFailure("non-image import must fail") { _ = try storage.importArtwork(from: invalid) }
        expect(try Data(contentsOf: storage.artworkURL) == persisted, "failed import must preserve prior artwork")
        expectFailure("empty image must fail") { _ = try EngineLibraryArtworkStorage.prepareImage(Data()) }
        expectFailure("directory import must fail") { _ = try storage.importArtwork(from: root) }
        let shortcut = root.appendingPathComponent("shortcut.png")
        try FileManager.default.createSymbolicLink(at: shortcut, withDestinationURL: input)
        expectFailure("file shortcut must fail") { _ = try storage.importArtwork(from: shortcut) }
        let oversized = root.appendingPathComponent("oversized.png")
        FileManager.default.createFile(atPath: oversized.path, contents: nil)
        let handle = try FileHandle(forWritingTo: oversized)
        try handle.truncate(atOffset: UInt64(EngineLibraryArtworkStorage.maximumFileBytes + 1))
        try handle.close()
        expectFailure("oversized file must fail before decode") { _ = try storage.importArtwork(from: oversized) }
        expect(try Data(contentsOf: storage.artworkURL) == persisted, "all rejected inputs must preserve previous data")
        // A valid, highly compressed image can exceed the pixel budget even
        // when its encoded byte count is small. Reject it before decoding.
        let excessivePixels = image(width: 10_000, height: 5_001)
        expect(excessivePixels.count < EngineLibraryArtworkStorage.maximumFileBytes,
               "pixel limit fixture should fit the byte budget")
        do {
            _ = try EngineLibraryArtworkStorage.prepareImage(excessivePixels)
            fatalError("excessive pixel count must fail")
        } catch EngineLibraryArtworkStorage.ArtworkError.imageTooLarge { checks += 1 }
        let unrelated = storage.directory.appendingPathComponent("unrelated-save.txt")
        try Data("preserve".utf8).write(to: unrelated)
        try storage.removeArtwork()
        expect(!storage.hasArtwork, "reset should restore default")
        expect(FileManager.default.fileExists(atPath: input.path), "reset must preserve original source")
        expect(try String(contentsOf: unrelated, encoding: .utf8) == "preserve", "reset must only remove its exact image")
        try storage.removeArtwork()
        expect(!storage.hasArtwork, "reset should be idempotent")
        print("PASS \(checks) library artwork validation checks")
    }
}
