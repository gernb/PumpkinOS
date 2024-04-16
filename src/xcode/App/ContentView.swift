//
//  ContentView.swift
//
//  Created by peter bohac on 4/15/24.
//

import Foundation
import OSLog
import SwiftUI

struct ContentView: View {
    let model = ViewModel()

    var body: some View {
        VStack {
            Image(systemName: "globe")
                .imageScale(.large)
                .foregroundStyle(.tint)
            Text("Hello, world!")
            ScrollView {
                VStack {
                    ForEach(model.logLines) { line in
                        Text(line.message)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
            }
        }
        .padding()
        .onAppear {
            runPitMain()
        }
    }

    func runPitMain(debugLevel: Int = 1, script: String = "main.lua") {
        guard let scriptPath = Bundle.main.path(forResource: script, ofType: nil) else {
            Logger.default.critical("Failed to get path for script: \(script, privacy: .public)")
            return
        }
        Task.detached(priority: .userInitiated) {
            let args = ["app", "-d", "\(debugLevel)", "-s", "libscriptlua.so", scriptPath]
            var cargs = args.map { strdup($0) }
            let result = pit_main(Int32(args.count), &cargs, nil, nil)
            cargs.forEach { free($0) }
            if result != 0 {
                Logger.default.critical("pit_main returned error status: \(result)")
            }
        }
    }

    @MainActor @Observable
    final class ViewModel {
        private(set) var logLines: [LogLine] = []

        private let pipe = Pipe()
        private let isDebuggerAttached: Bool = {
            var debuggerIsAttached = false

            var name: [Int32] = [CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()]
            var info: kinfo_proc = kinfo_proc()
            var infoSize = MemoryLayout<kinfo_proc>.size

            let success = name.withUnsafeMutableBytes { (nameBytePtr: UnsafeMutableRawBufferPointer) -> Bool in
                guard let nameBytesBlindMemory = nameBytePtr.bindMemory(to: Int32.self).baseAddress else { return false }
                return -1 != sysctl(nameBytesBlindMemory, 4, &info, &infoSize, nil, 0)
            }

            if !success {
                debuggerIsAttached = false
            }

            if !debuggerIsAttached && (info.kp_proc.p_flag & P_TRACED) != 0 {
                debuggerIsAttached = true
            }

            return debuggerIsAttached
        }()

        nonisolated init() {
            self.captureStdErr()
        }

        private nonisolated func captureStdErr() {
            if isDebuggerAttached == false {
                setvbuf(stderr, nil, _IONBF, 0)
                dup2(pipe.fileHandleForWriting.fileDescriptor, STDERR_FILENO)
                pipe.fileHandleForReading.readabilityHandler = { @Sendable [weak self] handle in
                    guard let log = String(data: handle.availableData, encoding: .utf8) else {
                        return
                    }
                    let lines = log
                        .trimmingCharacters(in: .whitespacesAndNewlines)
                        .components(separatedBy: .newlines)
                        .map(LogLine.init)
                    lines.forEach { line in
                        switch line.level {
                        case .trace: Logger.pumpkin.debug("\(line.message, privacy: .public)")
                        case .info: Logger.pumpkin.info("\(line.message, privacy: .public)")
                        case .error: Logger.pumpkin.critical("\(line.message, privacy: .public)")
                        case .unknown: Logger.pumpkin.info("\(line.message, privacy: .public)")
                        }
                    }
                    Task { @MainActor [weak self] in
                        self?.logLines += lines
                    }
                }
            }
        }
    }
}

struct LogLine: Identifiable, Sendable {
    enum Level {
        case trace, info, error, unknown
    }
    let id = UUID()
    let message: String
    let level: Level

    init(_ line: String) {
        self.message = String(line)
        let parts = line.split(separator: " ")
        if parts.count >= 3 {
            switch parts[2] {
            case "T": self.level = .trace
            case "I": self.level = .info
            case "E": self.level = .error
            default: self.level = .unknown
            }
        } else {
            self.level = .unknown
        }
    }
}

#Preview {
    ContentView()
}
