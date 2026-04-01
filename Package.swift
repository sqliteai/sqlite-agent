// swift-tools-version: 6.1
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "agent",
    platforms: [.macOS(.v11), .iOS(.v11)],
    products: [
        .library(
            name: "agent",
            targets: ["agent"])
    ],
    targets: [
        .binaryTarget(
            name: "agentBinary",
            url: "https://github.com/sqliteai/sqlite-agent/releases/download/0.1.8/agent-apple-xcframework-0.1.8.zip",
            checksum: "0000000000000000000000000000000000000000000000000000000000000000"
        ),
        .target(
            name: "agent",
            dependencies: ["agentBinary"],
            path: "packages/swift"
        ),
    ]
)
