// swift-tools-version: 5.9
// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

import PackageDescription

let package = Package(
    name: "LinkspanKit",
    products: [
        .library(name: "LinkspanKit", targets: ["LinkspanKit"]),
    ],
    targets: [
        .target(name: "LinkspanKit"),
        .testTarget(name: "LinkspanKitTests", dependencies: ["LinkspanKit"]),
    ]
)
