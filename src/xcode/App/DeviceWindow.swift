//
//  DeviceWindow.swift
//
//  Created by peter bohac on 4/16/24.
//

import SwiftUI

struct DeviceWindow: View {
    let info: CreateWindowInfo

    @Environment(Pit.self) private var pit
    @State private var title = "Untitled"
    @State private var mainWindow = Image(systemName: "xmark")
    @State private var isDragging = false

    var body: some View {
        VStack {
            mainWindow
                .frame(width: info.width, height: info.width)
                .focusable()
                .focusEffectDisabled()
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
        }
        .navigationTitle(title)
        .onAppear { title = info.title ?? title }
        .onDisappear {
            Task {
                await pit.stop()
            }
        }
        .task {
            guard let events = await WindowProvider.events?.subscribe() else { return }
            for await event in events {
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
                case .createWindow, .destroyWindow:
                    break
                }
            }
        }
    }
}

#Preview {
    DeviceWindow(info: .init(width: 300, height: 300, title: "Preview"))
        .environment(Pit())
}
