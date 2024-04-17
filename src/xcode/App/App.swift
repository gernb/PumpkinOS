//
//  App.swift
//
//  Created by peter bohac on 4/15/24.
//

import SwiftUI

@main
struct AppApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    let pit = Pit()

    init() {
        WindowProvider.initialize()
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environment(pit)
        }

        WindowGroup(id: "deviceWindow", for: CreateWindowInfo.self) { info in
            DeviceWindow(info: info.wrappedValue ?? .init(width: 320, height: 320, title: nil))
                .environment(pit)
        }
        .windowResizability(.contentSize)
    }
}

class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return true
    }
}

struct CreateWindowInfo: Codable, Hashable {
    let width: CGFloat
    let height: CGFloat
    let title: String?
}
