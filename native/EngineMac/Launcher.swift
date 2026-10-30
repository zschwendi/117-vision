import AppKit
import CryptoKit

enum LaunchError: LocalizedError {
    case invalid(String)
    var errorDescription: String? { if case let .invalid(message) = self { return message }; return nil }
}

func validate(_ folder: URL) throws {
    let fm = FileManager.default
    for name in ["halo.exe", "strings.dll", "maps", "shaders", "halo-vision-registry.txt"] {
        guard fm.fileExists(atPath: folder.appendingPathComponent(name).path) else {
            throw LaunchError.invalid("Choose a prepared Halo PC 1.10 folder containing \(name). See the download guide.")
        }
    }
    let executable = try Data(contentsOf: folder.appendingPathComponent("halo.exe"), options: .mappedIfSafe)
    let digest = SHA256.hash(data: executable).map { String(format: "%02x", $0) }.joined()
    guard digest == "c9acf0c469543283cfed6d7dc04ade976dbdfc7cb4532cf070386de169c19545" else {
        throw LaunchError.invalid("This executable is not the supported retail Halo PC 1.10 build.")
    }
}

@main
enum Launcher {
    static func main() {
        // Read-only package check; never starts the engine or writes game files.
        if CommandLine.arguments.count == 3 && CommandLine.arguments[1] == "--check-input" {
            do {
                try validate(URL(fileURLWithPath: CommandLine.arguments[2], isDirectory: true))
                print("Supported executable and required input paths verified.")
            } catch {
                fputs((error.localizedDescription + "\n"), stderr)
                exit(2)
            }
            return
        }
        let application = NSApplication.shared
        application.setActivationPolicy(.regular)
        application.activate(ignoringOtherApps: true)
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.prompt = "Play"
        panel.message = "Select your prepared Halo PC 1.10 working folder. The game stores saves and settings in this folder; use a separate copy of your original installation."
        guard panel.runModal() == .OK, let folder = panel.url else { return }
        do {
            try validate(folder)
            let process = Process()
            process.executableURL = Bundle.main.bundleURL.appendingPathComponent("Contents/MacOS/halo-host")
            process.arguments = [folder.path, "--quiet"]
            process.currentDirectoryURL = folder
            let logDirectory = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Logs")
            try FileManager.default.createDirectory(at: logDirectory, withIntermediateDirectories: true)
            let logURL = logDirectory.appendingPathComponent("117-macOS-preview.log")
            FileManager.default.createFile(atPath: logURL.path, contents: nil)
            let log = try FileHandle(forWritingTo: logURL)
            defer { try? log.close() }
            process.standardOutput = log
            process.standardError = log
            try process.run()
            process.waitUntilExit()
            if process.terminationStatus != 0 {
                throw LaunchError.invalid("The experimental Mac engine stopped (exit \(process.terminationStatus)). Its local log is in Library/Logs/117-macOS-preview.log. Keep game files and registration private.")
            }
        } catch {
            let alert = NSAlert()
            alert.messageText = "117 could not start"
            alert.informativeText = error.localizedDescription
            alert.runModal()
        }
    }
}
