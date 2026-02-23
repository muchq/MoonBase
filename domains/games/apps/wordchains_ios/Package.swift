// swift-tools-version: 6.0
// Package.swift — local development and testing only.
// The canonical build uses Bazel (see BUILD.bazel).

import PackageDescription

let package = Package(
    name: "WordChains",
    platforms: [.iOS(.v17), .macOS(.v14)],
    targets: [
        .target(
            name: "WordChains",
            path: "Sources",
            exclude: ["Resources", "App"],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .testTarget(
            name: "WordChainsTests",
            dependencies: ["WordChains"],
            path: "Tests",
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
    ]
)
