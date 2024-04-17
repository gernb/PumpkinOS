//
//  DeviceWindow.swift
//
//  Created by peter bohac on 4/16/24.
//

import SwiftUI

struct DeviceWindow: View {
    let window: WindowProvider.Window

    @Environment(Pit.self) private var pit
    @State private var title = "Untitled"
    @State private var mainWindow = Image(systemName: "xmark")
    @State private var isDragging = false

    var body: some View {
        VStack(spacing: 0) {
            mainWindow
                .frame(width: CGFloat(window.width), height: CGFloat(window.width))
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
            Divider()
            HStack(spacing: 30) {
                Button("Home", systemImage: "house.circle") {
                    WindowProvider.keyEvent(.home)
                }
                Button("Menu", systemImage: "filemenu.and.selection") {
                    WindowProvider.keyEvent(.menu)
                }
            }
            .padding(5)
            .labelStyle(.iconOnly)
            .buttonStyle(.borderless)
            .font(.largeTitle)
        }
        .navigationTitle(title)
        .onAppear {
            title = window.title ?? title
            mainWindow = window.image
        }
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
                    mainWindow = window.image
                case .createWindow, .destroyWindow:
                    break
                }
            }
        }
    }
}

extension WindowProvider.Window {
    var image: Image {
        let cgImage = buffer.withUnsafeMutableBytes { bufferPtr in
            let ctx = CGContext(
                data: bufferPtr.baseAddress,
                width: Int(width),
                height: Int(height),
                bitsPerComponent: 8,
                bytesPerRow: Int(width * pixelSize),
                space: CGColorSpace(name: CGColorSpace.sRGB)!,
                bitmapInfo: CGImageAlphaInfo.premultipliedFirst.rawValue
            )!
            return ctx.makeImage()!
        }
        return Image(cgImage, scale: 1, label: Text("Device Window"))
    }
}

#Preview {
    DeviceWindow(window: .empty)
        .environment(Pit())
}
