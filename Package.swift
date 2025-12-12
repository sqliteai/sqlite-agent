//
//  Package.swift
//  sqlite-agent
//
//  Created by Gioele Cantoni on 05/11/25.
//

// swift-tools-version: 6.1
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "agent",
    platforms: [.macOS(.v11), .iOS(.v11)],
    products: [
        // Products can be used to vend plugins, making them visible to other packages.
        .plugin(
            name: "agentPlugin",
            targets: ["agentPlugin"]),
        .library(
            name: "agent",
            targets: ["agent"])
    ],
    targets: [
        // Build tool plugin that invokes the Makefile
        .plugin(
            name: "agentPlugin",
            capability: .buildTool(),
            path: "packages/swift/plugin"
        ),
        // agent library target
        .target(
            name: "agent",
            dependencies: [],
            path: "packages/swift/extension",
            plugins: ["agentPlugin"]
        ),
    ]
)
