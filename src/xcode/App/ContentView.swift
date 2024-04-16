//
//  ContentView.swift
//
//  Created by peter bohac on 4/15/24.
//

import SwiftUI

struct ContentView: View {
    private let pit = Pit()
    @State private var logLines: [LogLine] = []
    @State private var title = ""
    @State private var mainWindow = Image(systemName: "xmark")
    @State private var isDragging = false
    @FocusState private var mainWindowFocused

    var body: some View {
        HStack {
            VStack {
                HStack {
                    if pit.running {
                        Button("Stop") {
                            Task {
                                await pit.stop()
                            }
                        }
                    } else {
                        Button("Start") {
                            pit.main()
                        }
                    }
                }
                .buttonStyle(.bordered)
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
                .focusable()
                .defaultScrollAnchor(.bottom)
            }

            VStack(alignment: .leading) {
                Text(title)
                    .frame(width: 320, alignment: .leading)
                mainWindow
                    .frame(width: 320, height: 320)
                    .focusable()
                    .focusEffectDisabled()
                    .focused($mainWindowFocused)
                    .gesture(
                        DragGesture(minimumDistance: 0)
                            .onChanged { event in
                                if isDragging {
                                    WindowProvider.mouseMove(to: event.location)
                                } else {
                                    WindowProvider.mouseDown(at: event.location)
                                    isDragging = true
                                }
                            }
                            .onEnded { event in
                                WindowProvider.mouseUp(at: event.location)
                                isDragging = false
                            }
                    )
                    .onKeyPress(phases: .all) { keyPress in
                        WindowProvider.keyEvent(keyPress)
                        return .handled
                    }
                    .padding()
                    .border(.secondary)
            }
        }
        .padding()
        .task {
            for await line in pit.logLines {
                logLines.append(line)
            }
        }
        .task {
            for await event in WindowProvider.events! {
                switch event {
                case .setTitle(_, let title):
                    self.title = title
                case .draw(let window):
                    let cgImage = window.buffer.withUnsafeMutableBytes { bufferPtr in
                        let ctx = CGContext(
                            data: bufferPtr.baseAddress,
                            width: Int(window.width),
                            height: Int(window.height),
                            bitsPerComponent: 8,
                            bytesPerRow: Int(window.width * window.pixelSize),
                            space: CGColorSpace(name: CGColorSpace.sRGB)!,
                            bitmapInfo: CGImageAlphaInfo.premultipliedFirst.rawValue
                        )!
                        return ctx.makeImage()!
                    }
                    mainWindow = Image(cgImage, scale: 1, label: Text("mainWindow"))
                case .createWindow:
                    mainWindowFocused = true
                case .destroyWindow:
                    mainWindowFocused = false
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
}
