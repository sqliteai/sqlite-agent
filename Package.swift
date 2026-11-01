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
            url: "https://github.com/sqliteai/sqlite-agent/releases/download/0.2.0/agent-apple-xcframework-0.2.0.zip",
            checksum: "984dbd44bfa0bfafe7e1de6094b450c1738f62382f662e8638ed9dec9ab90d43"
        ),
        .target(
            name: "agent",
            dependencies: ["agentBinary"],
            path: "packages/swift"
        ),
    ]
)
