//
//  App.swift
//
//  Created by peter bohac on 4/15/24.
//

import SwiftUI

@main
struct AppApp: App {
    init() {
        WindowProvider.initialize()
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}
