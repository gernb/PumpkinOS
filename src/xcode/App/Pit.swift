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
    private(set) var logId = UUID()
    private(set) var logLines: AsyncStream<LogLine>?
    @MainActor private(set) var running = false

    private var mainLoopTask: Task<Void, Never>?
    private static var wp = window_provider_t(
        create: { WindowProvider.create(encoding: $0, widthPtr: $1, heightPtr: $2, xfactor: $3, yfactor: $4, rotate: $5, fullscreen: $6, software: $7, dataPtr: $8) },
        event: nil,
        destroy: { WindowProvider.destroy(windowPtr: $0) },
        erase: nil,
        render: nil,
        background: nil,
        create_texture: { WindowProvider.createTexture(windowPtr: $0, width: $1, height: $2) },
        destroy_texture: { WindowProvider.destroyTexture(windowPtr: $0, texturePtr: $1) },
        update_texture: { WindowProvider.updateTexture(windowPtr: $0, texturePtr: $1, rawPtr: $2) },
        draw_texture: nil,
        status: nil,
        title: { WindowProvider.title(windowPtr: $0, titlePtr: $1) },
        clipboard: nil,
        event2: { WindowProvider.event2(windowPtr: $0, wait: $1, arg1Ptr: $2, arg2Ptr: $3) },
        update: nil,
        draw_texture_rect: { WindowProvider.drawTexture(windowPtr: $0, texturePtr: $1, tx: $2, ty: $3, w: $4, h: $5, x: $6, y: $7) },
        update_texture_rect: { WindowProvider.updateTexture(windowPtr: $0, texturePtr: $1, srcPtr: $2, tx: $3, ty: $4, w: $5, h: $6) },
        move: nil,
        average: nil,
        data: nil
    )

    enum Constants {
        static let tempScript = URL.temporaryDirectory.appending(path: "main.lua")
        static let libraryDir = URL.libraryDirectory.appending(path: Bundle.main.bundleIdentifier!)
        static let vfsUrl = libraryDir.appending(path: "vfs/") // The trailing / is necessary
        static let cachesDir =  URL.cachesDirectory.appending(path: Bundle.main.bundleIdentifier!)
        static let logUrl = cachesDir.appending(path: "pumpkin.log")
    }

    init() {
        self.createVFSDirectory()
    }

    deinit {
        mainLoopTask?.cancel()
    }

    @MainActor
    func main(debugLevel: Int = 1, width: Int = 320, height: Int = 320) {
        guard
            let templateURL = Bundle.main.url(forResource: "main", withExtension: "lua"),
            let template = try? String(contentsOf: templateURL)
        else {
            Logger.default.critical("Failed to get script template")
            return
        }
        let script = template
            .replacingOccurrences(of: "{width}", with: "\(width)")
            .replacingOccurrences(of: "{height}", with: "\(height)")
        do {
            try script.write(to: Constants.tempScript, atomically: true, encoding: .utf8)
        } catch {
            Logger.default.critical("Failed to write lua script: \(String(describing: error), privacy: .public)")
            return
        }
        WindowProvider.reset()
        running = true
        createLogStream()
        mainLoopTask = Task.detached(priority: .userInitiated) { [weak self] in
            guard let self else { return }
            let args = ["app", "-d", "\(debugLevel)", "-f", Constants.logUrl.path, "-s", "libscriptlua.so", Constants.tempScript.path]
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
    func stop() async {
        WindowProvider.stop()
        try? await Task.sleep(for: .milliseconds(500))
        mainLoopTask?.cancel()
        mainLoopTask = nil
        running = false
    }

    private static func mainCallback(enginePtr: Int32, data: UnsafeMutableRawPointer?) {
        guard let data else { return }
        let instance: Pit = Unmanaged.fromOpaque(data).takeUnretainedValue()
        instance.mountVFS()
        instance.registerWindowProvider(enginePtr: enginePtr)
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

    private func registerWindowProvider(enginePtr: Int32) {
        let cStr = strdup(WINDOW_PROVIDER)
        if script_set_pointer(enginePtr, cStr, &Self.wp) != 0 {
            Logger.default.critical("Failed to register window provider")
        }
        free(cStr)
    }

    @MainActor
    private func createLogStream() {
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
        self.logId = .init()
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

extension window_provider_t: @unchecked Sendable {}
