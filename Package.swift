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
            checksum: "951d59f2953a9fcca946b8b2435057b2e5b14fb504a9ea2330ec381534853a9c"
        ),
        .target(
            name: "agent",
            dependencies: ["agentBinary"],
            path: "packages/swift"
        ),
    ]
)
