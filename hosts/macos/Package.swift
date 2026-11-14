// swift-tools-version: 5.9
// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

import PackageDescription

let package = Package(
    name: "OlympiumKit",
    products: [
        .library(name: "OlympiumKit", targets: ["OlympiumKit"]),
    ],
    targets: [
        .target(name: "OlympiumKit"),
        .testTarget(name: "OlympiumKitTests", dependencies: ["OlympiumKit"]),
    ]
)
