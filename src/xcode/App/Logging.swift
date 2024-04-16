//
//  Logging.swift
//
//  Created by peter bohac on 4/15/24.
//

import OSLog

extension Logger {
    static var `default`: Self { .init(subsystem: Bundle.main.bundleIdentifier!, category: "default") }
    static var pumpkin: Self { .init(subsystem: Bundle.main.bundleIdentifier!, category: "pumpkin") }
}
