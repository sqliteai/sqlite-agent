//
//  agent.swift
//  sqliteagent
//
//  Created by Gioele Cantoni on 05/11/25.
//

// This file serves as a placeholder for the agent target.
// The actual SQLite extension is built using the Makefile through the build plugin.

import Foundation

/// Placeholder structure for agent
public struct agent {
    /// Returns the path to the built agent dylib inside the XCFramework
    public static var path: String {
        #if os(macOS)
        return "agent.xcframework/macos-arm64_x86_64/agent.framework/agent"
        #elseif targetEnvironment(simulator)
        return "agent.xcframework/ios-arm64_x86_64-simulator/agent.framework/agent"
        #else
        return "agent.xcframework/ios-arm64/agent.framework/agent"
        #endif
    }
}
