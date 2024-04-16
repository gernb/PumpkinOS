//
//  ContentView.swift
//
//  Created by peter bohac on 4/15/24.
//

import SwiftUI

struct ContentView: View {
    enum LogLevel: Int, CaseIterable {
        case error, info, debug

        var label: String {
            switch self {
            case .debug: "Debug"
            case .info: "Info"
            case .error: "Error"
            }
        }
    }

    @AppStorage("logLevel") private var logLevel = LogLevel.info
    @AppStorage("width") private var width: Int = 1024
    @AppStorage("height") private var height: Int = 600
    @State private var logLines: [LogLine] = []

    @Environment(Pit.self) private var pit
    @Environment(\.openWindow) private var openWindow
    @Environment(\.dismissWindow) private var dismissWindow

    var body: some View {
        VStack {
            HStack {
                Picker("Logging level:", selection: $logLevel) {
                    ForEach(LogLevel.allCases, id: \.self) { level in
                        Text(level.label).tag(level)
                    }
                }
                .frame(maxWidth: 200)
                .disabled(pit.running)
                LabeledContent("Width:") {
                    TextField("Width", value: $width, format: .number, prompt: Text("Width"))
                }
                .frame(maxWidth: 200)
                .disabled(pit.running)
                LabeledContent("Height:") {
                    TextField("Height", value: $height, format: .number, prompt: Text("Height"))
                }
                .frame(maxWidth: 200)
                .disabled(pit.running)

                Spacer()

                if pit.running {
                    Button("Stop") {
                        Task {
                            await pit.stop()
                        }
                    }
                } else {
                    Button("Start") {
                        pit.main(debugLevel: logLevel.rawValue, width: width, height: height)
                    }
                }
            }
            .buttonStyle(.borderedProminent)

            ScrollView {
                VStack {
                    ForEach(logLines) { line in
                        Text(line.message)
                            .foregroundStyle(line.level.style)
                    }
                    .fontDesign(.monospaced)
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
            }
            .textSelection(.enabled)
            .focusable()
            .defaultScrollAnchor(.bottom)
        }
        .padding()
        .task(id: pit.logId) {
            logLines.append(.init("--------------------------------------"))
            if let log = pit.logLines {
                for await line in log {
                    logLines.append(line)
                }
            }
        }
        .task {
            guard let events = await WindowProvider.events?.subscribe() else { return }
            for await event in events {
                switch event {
                case .setTitle, .draw:
                    break
                case .createWindow(let window):
                    let info = CreateWindowInfo(
                        width: CGFloat(window.width),
                        height: CGFloat(window.height),
                        title: window.title
                    )
                    openWindow(id: "deviceWindow", value: info)
                case .destroyWindow(let window):
                    let info = CreateWindowInfo(
                        width: CGFloat(window.width),
                        height: CGFloat(window.height),
                        title: window.title
                    )
                    dismissWindow(id: "deviceWindow", value: info)
                }
            }
        }
    }
}

private extension LogLine.Level {
    var style: any ShapeStyle {
        switch self {
        case .trace: .secondary
        case .info: .primary
        case .error: .red
        case .unknown: .orange
        }
    }
}

#Preview {
    ContentView()
        .environment(Pit())
}
