//
//  App.swift
//
//  Created by peter bohac on 4/15/24.
//

import SwiftUI

@main
struct AppApp: App {
    let pit = Pit()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environment(pit)
                .onAppear {
                    pit.main()
                }
        }
    }
}
