// agent.swift
// Provides the path to the agent SQLite extension for use with sqlite3_load_extension.

import Foundation

public struct agent {
    /// Returns the absolute path to the agent dylib for use with sqlite3_load_extension.
    public static var path: String {
        #if os(macOS)
        return Bundle.main.bundlePath + "/Contents/Frameworks/agent.framework/agent"
        #else
        return Bundle.main.bundlePath + "/Frameworks/agent.framework/agent"
        #endif
    }
}
