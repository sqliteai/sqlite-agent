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
            url: "https://github.com/sqliteai/sqlite-agent/releases/download/0.1.9/agent-apple-xcframework-0.1.9.zip",
            checksum: "95c3456b769af2bab89fef15bc349f30f4e9776a24108df6c27a435c9ea5b4ad"
        ),
        .target(
            name: "agent",
            dependencies: ["agentBinary"],
            path: "packages/swift"
        ),
    ]
)
