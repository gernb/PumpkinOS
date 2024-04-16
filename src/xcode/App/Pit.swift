//
//  Pit.swift
//
//  Created by peter bohac on 4/15/24.
//

@preconcurrency import Foundation
import Observation
import OSLog

@Observable
final class Pit: Sendable {
    let logLines: AsyncStream<LogLine>
    @MainActor private(set) var running = false

    private var mainLoopTask: Task<Void, Never>?

    enum Constants {
        static let libraryDir = URL.libraryDirectory.appending(path: Bundle.main.bundleIdentifier!)
        static let vfsUrl = libraryDir.appending(path: "vfs/") // The trailing / is necessary
        static let cachesDir =  URL.cachesDirectory.appending(path: Bundle.main.bundleIdentifier!)
        static let logUrl = cachesDir.appending(path: "pumpkin.log")
    }

    init() {
        self.logLines = .init { publisher in
            try? FileManager.default.createDirectory(at: Constants.cachesDir, withIntermediateDirectories: true)
            try? "".write(to: Constants.logUrl, atomically: true, encoding: .utf8)
            let fileHandle = try! FileHandle(forReadingFrom: Constants.logUrl) // swiftlint:disable:this force_try
            let source = DispatchSource.makeFileSystemObjectSource(
                fileDescriptor: fileHandle.fileDescriptor,
                eventMask: .extend,
                queue: .global()
            )
            let logger = Logger.pit
            source.setEventHandler {
                guard
                    let string = String(data: fileHandle.availableData, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines),
                    string.isEmpty == false
                else { return }
                string.components(separatedBy: .newlines)
                    .map(LogLine.init)
                    .forEach { logLine in
                        switch logLine.level {
                        case .trace: logger.debug("\(logLine.message, privacy: .public)")
                        case .info: logger.info("\(logLine.message, privacy: .public)")
                        case .error: logger.critical("\(logLine.message, privacy: .public)")
                        case .unknown: logger.info("\(logLine.message, privacy: .public)")
                        }
                        publisher.yield(logLine)
                    }
            }
            publisher.onTermination = { _ in
                source.cancel()
                try? fileHandle.close()
            }
            source.resume()
        }
        self.createVFSDirectory()
    }

    deinit {
        mainLoopTask?.cancel()
    }

    @MainActor
    func main(debugLevel: Int = 1, script: String = "main.lua") {
        guard let scriptPath = Bundle.main.path(forResource: script, ofType: nil) else {
            Logger.default.critical("Failed to get path for script: \(script, privacy: .public)")
            return
        }
        running = true
        mainLoopTask = Task.detached(priority: .userInitiated) { [weak self] in
            guard let self else { return }
            let args = ["app", "-d", "\(debugLevel)", "-f", Constants.logUrl.path, "-s", "libscriptlua.so", scriptPath]
            var cargs = args.map { strdup($0) }
            defer { cargs.forEach { free($0) } }
            let result = pit_main(
                Int32(args.count),
                &cargs,
                { Pit.mainCallback(enginePtr: $0, data: $1) },
                Unmanaged.passUnretained(self).toOpaque()
            )
            if result != 0 {
                Logger.default.critical("pit_main returned error status: \(result)")
            }
            Task { @MainActor [weak self] in
                self?.running = false
            }
        }
    }

    @MainActor
    func stop(status: Int32 = 0) async {
        sys_set_finish(status)
        try? await Task.sleep(for: .milliseconds(500))
        mainLoopTask?.cancel()
        mainLoopTask = nil
        running = false
    }

    private static func mainCallback(enginePtr: Int32, data: UnsafeMutableRawPointer?) {
        guard let data else { return }
        let instance: Pit = Unmanaged.fromOpaque(data).takeUnretainedValue()
        instance.mountVFS()
    }

    private func mountVFS() {
        let local = strdup(Constants.vfsUrl.path())
        let virtual = strdup("/")
        if vfs_local_mount(local, virtual) != 0 {
            Logger.default.critical("Failed to mount virtual file system")
        }
        free(virtual)
        free(local)
    }

    private func createVFSDirectory() {
        if FileManager.default.fileExists(atPath: Constants.libraryDir.path) == false {
            do {
                try FileManager.default.createDirectory(at: Constants.libraryDir, withIntermediateDirectories: true)
                try FileManager.default.copyItem(
                    at: Bundle.main.resourceURL!.appending(path: "PumpkinVFS"),
                    to: Constants.vfsUrl
                )
            } catch {
                Logger.default.critical("Failed to create VFS folders in library: \(String(describing: error), privacy: .public)")
            }
        }
    }
}
