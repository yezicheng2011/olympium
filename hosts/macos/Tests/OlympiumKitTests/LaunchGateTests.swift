// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

import XCTest
@testable import OlympiumKit

final class LaunchGateTests: XCTestCase {
    func testTamperedAppleCellsStillRefuse() {
        XCTAssertTrue(LaunchGate.mustRefuse(host: "linux", guest: "macos", level: "ready"))
        XCTAssertTrue(LaunchGate.mustRefuse(host: "android", guest: "ios", level: "engine"))
        XCTAssertTrue(LaunchGate.mustRefuse(host: "windows", guest: "linux", level: "refused"))
        XCTAssertFalse(LaunchGate.mustRefuse(host: "macos", guest: "macos", level: "ready"))
        XCTAssertFalse(LaunchGate.mustRefuse(host: "macos", guest: "ios", level: "limited"))
        XCTAssertFalse(LaunchGate.mustRefuse(host: "linux", guest: "windows", level: "engine"))
        XCTAssertTrue(LaunchGate.mustRefuse(host: "android", guest: "openharmony", level: "ready"))
        XCTAssertTrue(LaunchGate.mustRefuse(host: "openharmony", guest: "windows", level: "engine"))
        XCTAssertFalse(LaunchGate.mustRefuse(host: "linux", guest: "openharmony", level: "limited"))
        XCTAssertFalse(LaunchGate.mustRefuse(host: "openharmony", guest: "openharmony", level: "ready"))
        XCTAssertTrue(LaunchGate.refusalMessage(host: "linux").contains("Olympium will not run macOS or iOS software"))
        XCTAssertTrue(LaunchGate.refusalMessage(host: "android", guest: "openharmony").contains("does not translate a HAP"))
    }

    func testCheckedInDocument() throws {
        let root = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
        let url = root.appendingPathComponent("share/compatibility.json")
        let data = try Data(contentsOf: url)
        let document = try CompatibilityDocument.load(from: data)
        XCTAssertEqual(document.schema, 1)
        XCTAssertEqual(document.product, "Olympium")
        XCTAssertEqual(document.license, "Apache-2.0")
        XCTAssertEqual(document.cells.count, 30)
        for cell in document.cells where (cell.guest == "macos" || cell.guest == "ios") && cell.host != "macos" {
            XCTAssertEqual(cell.level, "refused")
            XCTAssertTrue(LaunchGate.mustRefuse(host: cell.host, guest: cell.guest, level: cell.level))
        }
        XCTAssertEqual(document.cell(host: "linux", guest: "linux")?.level, "ready")
        XCTAssertEqual(document.cell(host: "macos", guest: "ios")?.level, "limited")
    }

    func testBundledEngineIsPreferredOverPath() throws {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let exe = dir.appendingPathComponent("olympium")
        try Data("#!/bin/sh\n".utf8).write(to: exe)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: exe.path)
        let found = EngineLocator.locate(environment: ["PATH": "/usr/bin"], bundledPath: exe.path)
        XCTAssertEqual(found, exe.path)
        let missing = dir.appendingPathComponent("missing").path
        XCTAssertNil(EngineLocator.locate(environment: ["PATH": "/usr/bin"], bundledPath: missing))
        let bundle = EngineLocator.bundledExecutable(bundleURL: URL(fileURLWithPath: "/Applications/Olympium.app"))
        XCTAssertEqual(bundle, "/Applications/Olympium.app/Contents/MacOS/olympium")
    }
}
