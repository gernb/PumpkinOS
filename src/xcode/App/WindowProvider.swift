//
//  WindowProvider.swift
//
//  Created by peter bohac on 4/16/24.
//

import Foundation
import OSLog
import SwiftUI

struct WindowProvider {
    enum Event: Sendable {
        case createWindow(Window)
        case destroyWindow(Window)
        case setTitle(Window, String)
        case draw(Window)
    }

    enum Input {
        case keyDown(Int32)
        case keyUp(Int32)
        case mouseDown
        case mouseUp
        case mouseMove(x: Int32, y: Int32)
        case stop
    }

    @MainActor
    static private(set) var events: AsyncSharedStream<Event>?
    @MainActor
    private static var publisher: AsyncStream<Event>.Continuation?
    private static var inputEvents: [Input] = []

    @MainActor
    static func initialize() {
        let (events, publisher) = AsyncStream<Event>.makeStream()
        self.events = .init(events)
        self.publisher = publisher
    }

    @MainActor
    static func reset() {
        inputEvents.removeAll()
    }

    @MainActor
    static func keyEvent(_ keyPress: KeyPress) {
        let code: Int32 = switch keyPress.key {
        case .tab: 9
        case .home: WINDOW_KEY_HOME
        case .leftArrow: WINDOW_KEY_LEFT
        case .upArrow: WINDOW_KEY_UP
        case .rightArrow: WINDOW_KEY_RIGHT
        case .downArrow: WINDOW_KEY_DOWN
        default:
            switch keyPress.key.character {
            case "\u{7F}": 8    // Delete key
            case "\r": 13       // Return key
            case "\u{F704}": WINDOW_KEY_F1
            case "\u{F705}": WINDOW_KEY_F2
            case "\u{F706}": WINDOW_KEY_F3
            case "\u{F707}": WINDOW_KEY_F4
            case "\u{F708}": WINDOW_KEY_F5
            default: Int32(keyPress.key.character.asciiValue ?? 0)
            }
        }
        switch keyPress.phase {
        case .down, .repeat:
            inputEvents.append(.keyDown(code))
        case .up:
            inputEvents.append(.keyUp(code))
        default:
            break
        }
    }

    @MainActor
    static func mouseDown(at location: CGPoint) {
        inputEvents.append(.mouseMove(x: Int32(location.x), y: Int32(location.y)))
        inputEvents.append(.mouseDown)
    }

    @MainActor
    static func mouseMove(to location: CGPoint) {
        inputEvents.append(.mouseMove(x: Int32(location.x), y: Int32(location.y)))
    }

    @MainActor
    static func mouseUp(at location: CGPoint) {
        inputEvents.append(.mouseMove(x: Int32(location.x), y: Int32(location.y)))
        inputEvents.append(.mouseUp)
    }

    @MainActor
    static func stop() {
        inputEvents.append(.stop)
    }

    static func create(
        encoding: Int32,
        widthPtr: UnsafeMutablePointer<Int32>?,
        heightPtr: UnsafeMutablePointer<Int32>?,
        xfactor: Int32,
        yfactor: Int32,
        rotate: Int32,
        fullscreen: Int32,
        software: Int32,
        dataPtr: UnsafeMutableRawPointer?
    ) -> UnsafeMutablePointer<window_t?>? {
        let width = widthPtr?.pointee ?? 0
        let height = heightPtr?.pointee ?? 0
        let window = Window(
            encoding: encoding,
            width: width,
            height: height
        )
        Logger.wp.debug("Create window: \(window.id); encoding=\(encoding), width=\(width), height=\(height)")
        Task { @MainActor in
            _ = publisher?.yield(.createWindow(window))
        }
        return Unmanaged.passRetained(window).toOpaque().assumingMemoryBound(to: window_t?.self)
    }

    static func destroy(
        windowPtr: UnsafeMutablePointer<window_t?>?
    ) -> Int32 {
        guard let windowPtr else { return -1 }
        let window: Window = Unmanaged.fromOpaque(UnsafeRawPointer(windowPtr)).takeRetainedValue()
        Logger.wp.debug("Destroy window: \(window.id)")
        Task { @MainActor in
            _ = publisher?.yield(.destroyWindow(window))
        }
        return 0
    }

    static func title(
        windowPtr: UnsafeMutablePointer<window_t?>?,
        titlePtr: UnsafeMutablePointer<CChar>?
    ) {
        guard let windowPtr, let titlePtr else { return }
        let window: Window = Unmanaged.fromOpaque(UnsafeRawPointer(windowPtr)).takeUnretainedValue()
        let title = String(cString: titlePtr)
        Logger.wp.debug("Title window: \(window.id); '\(title)'")
        window.title = title
        Task { @MainActor in
            _ = publisher?.yield(.setTitle(window, title))
        }
    }

    static func createTexture(
        windowPtr: UnsafeMutablePointer<window_t?>?,
        width: Int32,
        height: Int32
    ) -> OpaquePointer? {
        guard let windowPtr else { return nil }
        let window: Window = Unmanaged.fromOpaque(UnsafeRawPointer(windowPtr)).takeUnretainedValue()
        let texture = Texture(width: width, height: height, pixelSize: window.pixelSize)
        Logger.wp.debug("Create texture: \(texture.id)")
        return .init(Unmanaged.passRetained(texture).toOpaque())
    }

    static func destroyTexture(
        windowPtr: UnsafeMutablePointer<window_t?>?,
        texturePtr: OpaquePointer?
    ) -> Int32 {
        guard let texturePtr else { return 0 }
        let texture: Texture = Unmanaged.fromOpaque(UnsafeRawPointer(texturePtr)).takeRetainedValue()
        Logger.wp.debug("Destroy texture: \(texture.id)")
        return 0
    }

    static func updateTexture(
        windowPtr: UnsafeMutablePointer<window_t?>?,
        texturePtr: OpaquePointer?,
        rawPtr: UnsafeMutablePointer<UInt8>?
    ) -> Int32 {
        guard let texturePtr, let rawPtr else { return -1 }
        let texture: Texture = Unmanaged.fromOpaque(UnsafeRawPointer(texturePtr)).takeUnretainedValue()
        Logger.wp.debug("Update texture from raw: \(texture.id)")
        texture.buffer.withUnsafeMutableBytes { bufferPtr in
            let src = UnsafeRawBufferPointer(start: rawPtr, count: texture.size)
            bufferPtr.copyMemory(from: src)
        }
        return 0
    }

    static func updateTexture(
        windowPtr: UnsafeMutablePointer<window_t?>?,
        texturePtr: OpaquePointer?,
        srcPtr: UnsafeMutablePointer<UInt8>?,
        tx: Int32,
        ty: Int32,
        w: Int32,
        h: Int32
    ) -> Int32 {
        guard let windowPtr, let texturePtr, let srcPtr else { return -1 }
        let window: Window = Unmanaged.fromOpaque(UnsafeRawPointer(windowPtr)).takeUnretainedValue()
        let texture: Texture = Unmanaged.fromOpaque(UnsafeRawPointer(texturePtr)).takeUnretainedValue()
        Logger.wp.debug("Update texture from src: \(texture.id)")
        let len = Int(w * window.pixelSize)
        let pitch = Int(texture.width * window.pixelSize)
        texture.buffer.withUnsafeMutableBytes { bufferPtr in
            let start = Int((ty * texture.width + tx) * window.pixelSize)
            let destAddr = bufferPtr.baseAddress!.advanced(by: start)
            var dest = UnsafeMutableRawBufferPointer(start: destAddr, count: bufferPtr.count - start)
            let srcAddr = srcPtr.advanced(by: start)
            var src = UnsafeRawBufferPointer(start: srcAddr, count: len)
            for _ in 0 ..< h {
                dest.copyMemory(from: src)
                let destAddr = dest.baseAddress!.advanced(by: pitch)
                dest = UnsafeMutableRawBufferPointer(start: destAddr, count: dest.count - pitch)
                let srcAddr = src.baseAddress!.advanced(by: pitch)
                src = UnsafeRawBufferPointer(start: srcAddr, count: len)
            }
        }
        return 0
    }

    static func drawTexture(
        windowPtr: UnsafeMutablePointer<window_t?>?,
        texturePtr: OpaquePointer?,
        tx: Int32,
        ty: Int32,
        w: Int32,
        h: Int32,
        x: Int32,
        y: Int32
    ) -> Int32 {
        guard let windowPtr, let texturePtr else { return -1 }
        let window: Window = Unmanaged.fromOpaque(UnsafeRawPointer(windowPtr)).takeUnretainedValue()
        let texture: Texture = Unmanaged.fromOpaque(UnsafeRawPointer(texturePtr)).takeUnretainedValue()
        Logger.wp.debug("Draw texture rect: \(texture.id)")
        var (tx, ty, w, h, x, y) = (tx, ty, w, h, x, y)
        if x < 0 {
            let diff = 0 - x
            tx += diff
            w -= diff
            x += diff
        }
        if y < 0 {
            let diff = 0 - y
            ty += diff
            h -= diff
            y += diff
        }
        if (x + w) > window.width {
            w = window.width - x
        }
        if (y + h) > window.height {
            h = window.height - y
        }
        if w <= 0 || h <= 0 || x >= window.width || y >= window.height {
            // nothing to render
            return 0
        }

        let len = Int(w * window.pixelSize)
        let windowPitch = Int(window.width * window.pixelSize)
        let texturePitch = Int(texture.width * window.pixelSize)
        window.buffer.withUnsafeMutableBytes { windowBuffer in
            texture.buffer.withUnsafeBytes { textureBuffer in
                var start = Int((y * window.width + x) * window.pixelSize)
                let destAddr = windowBuffer.baseAddress!.advanced(by: start)
                var dest = UnsafeMutableRawBufferPointer(start: destAddr, count: windowBuffer.count - start)
                start = Int((ty * texture.width + tx) * window.pixelSize)
                let srcAddr = textureBuffer.baseAddress!.advanced(by: start)
                var src = UnsafeRawBufferPointer(start: srcAddr, count: len)
                for _ in 0 ..< h {
                    dest.copyMemory(from: src)
                    let destAddr = dest.baseAddress!.advanced(by: windowPitch)
                    dest = UnsafeMutableRawBufferPointer(start: destAddr, count: dest.count - windowPitch)
                    let srcAddr = src.baseAddress!.advanced(by: texturePitch)
                    src = UnsafeRawBufferPointer(start: srcAddr, count: len)
                }
            }
        }
        Task { @MainActor in
            _ = publisher?.yield(.draw(window))
        }
        return 0
    }

    static func event2(
        windowPtr: UnsafeMutablePointer<window_t?>?,
        wait: Int32,
        arg1Ptr: UnsafeMutablePointer<Int32>?,
        arg2Ptr: UnsafeMutablePointer<Int32>?
    ) -> Int32 {
        guard inputEvents.isEmpty == false else { return 0 }
        let event = inputEvents.removeFirst()
        switch event {
        case .keyDown(let code):
            Logger.wp.debug("Window key down: \(code)")
            arg1Ptr?.pointee = code
            arg2Ptr?.pointee = 0
            return WINDOW_KEYDOWN
        case .keyUp(let code):
            Logger.wp.debug("Window key up: \(code)")
            arg1Ptr?.pointee = code
            arg2Ptr?.pointee = 0
            return WINDOW_KEYUP
        case .mouseDown:
            arg1Ptr?.pointee = 1
            arg2Ptr?.pointee = 0
            return WINDOW_BUTTONDOWN
        case .mouseMove(let x, let y):
            arg1Ptr?.pointee = x
            arg2Ptr?.pointee = y
            return WINDOW_MOTION
        case .mouseUp:
            arg1Ptr?.pointee = 1
            arg2Ptr?.pointee = 0
            return WINDOW_BUTTONUP
        case .stop:
            return -1
        }
    }

    final class Window: Sendable {
        let id = UUID()
        let encoding: Int32
        let width: Int32
        let height: Int32
        var title: String?
        let size: Int
        var buffer: [UInt8]

        var pixelSize: Int32 { 4 }

        init(encoding: Int32, width: Int32, height: Int32) {
            assert(encoding == ENC_RGBA, "Only RGBA is currently supported")
            self.encoding = encoding
            self.width = width
            self.height = height
            self.size = Int(width * height) * 4
            self.buffer = Array(repeating: 0, count: size)
        }
    }

    final class Texture: Sendable {
        let id = UUID()
        let width: Int32
        let height: Int32
        let size: Int
        var buffer: [UInt8]

        init(width: Int32, height: Int32, pixelSize: Int32) {
            self.width = width
            self.height = height
            self.size = Int(width * height * pixelSize)
            self.buffer = Array(repeating: 0, count: size)
        }
    }
}
