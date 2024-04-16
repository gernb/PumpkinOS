//
//  ContentView.swift
//
//  Created by peter bohac on 4/15/24.
//

import SwiftUI

struct ContentView: View {
    private let pit = Pit()
    @State private var logLines: [LogLine] = []

    var body: some View {
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
            .defaultScrollAnchor(.bottom)
        }
        .padding()
        .task {
            for await line in pit.logLines {
                logLines.append(line)
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
