//
//  ContentView.swift
//
//  Created by peter bohac on 4/15/24.
//

import SwiftUI

struct ContentView: View {
    @Environment(Pit.self) private var pit

    var body: some View {
        VStack {
            Image(systemName: "globe")
                .imageScale(.large)
                .foregroundStyle(.tint)
            Text("Hello, world!")
            ScrollView {
                VStack {
                    ForEach(pit.logLines) { line in
                        Text(line.message)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
            }
        }
        .padding()
    }
}

#Preview {
    ContentView()
        .environment(Pit())
}
