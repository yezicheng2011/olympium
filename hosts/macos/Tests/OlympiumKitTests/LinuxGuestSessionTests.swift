// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

import XCTest
@testable import OlympiumKit

final class LinuxGuestSessionTests: XCTestCase {
    func testMissingQemuRefusesBoot() async {
        let session = LinuxGuestSession.evaluate(
            hostArchitecture: "arm64",
            searchPaths: ["/absent"],
            isExecutable: { _ in false }
        )
        XCTAssertFalse(session.canBoot)
        XCTAssertEqual(session.qemuCommand, "qemu-system-aarch64")
        XCTAssertTrue(session.status.contains("does not include QEMU"))
        XCTAssertTrue(session.status.contains("brew install qemu"))
        let controller = LinuxGuestController(session: session)
        let runner = RecordingRunner()
        let code = await controller.start(enginePath: "/engine", isExecutable: { _ in true }, runner: runner)
        XCTAssertNil(code)
        XCTAssertEqual(controller.outcome, .blocked)
        XCTAssertFalse(runner.started)
        XCTAssertFalse(controller.didStartRunner)
    }

    func testIntelMacLooksForTheX86Qemu() {
        let session = LinuxGuestSession.evaluate(
            hostArchitecture: "x86_64",
            searchPaths: ["/opt/qemu/bin"],
            isExecutable: { $0 == "/opt/qemu/bin/qemu-system-x86_64" }
        )
        XCTAssertTrue(session.canBoot)
        XCTAssertEqual(session.qemuPath, "/opt/qemu/bin/qemu-system-x86_64")
        XCTAssertEqual(session.arguments, ["guest", "--guest", "linux"])
    }

    func testProgressSuccessAndFailureUseTheSameController() async throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("olympium-guest-harness-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let qemu = root.appendingPathComponent("qemu-system-aarch64")
        try Data().write(to: qemu)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: qemu.path)
        let session = LinuxGuestSession.evaluate(
            hostArchitecture: "arm64",
            searchPaths: [root.path],
            isExecutable: { FileManager.default.isExecutableFile(atPath: $0) }
        )
        XCTAssertTrue(session.canBoot)

        let success = try executableScript(
            root.appendingPathComponent("success.sh"),
            body: "echo downloaded 3 / 9 >&2\necho olympium-linux-guest-ok\n"
        )
        let ok = LinuxGuestController(session: session)
        let okCode = await ok.start(enginePath: success, isExecutable: { FileManager.default.isExecutableFile(atPath: $0) }, runner: FoundationGuestRunner())
        XCTAssertEqual(okCode, 0)
        XCTAssertEqual(ok.outcome, .finished)
        XCTAssertTrue(ok.log.contains("downloaded 3 / 9"))
        XCTAssertTrue(ok.log.contains("olympium-linux-guest-ok"))
        XCTAssertTrue(ok.status.contains("finished"))

        let failing = try executableScript(
            root.appendingPathComponent("fail.sh"),
            body: "echo downloaded 1 / 4 >&2\nexit 7\n"
        )
        let bad = LinuxGuestController(session: session)
        let badCode = await bad.start(enginePath: failing, isExecutable: { FileManager.default.isExecutableFile(atPath: $0) }, runner: FoundationGuestRunner())
        XCTAssertEqual(badCode, 7)
        XCTAssertEqual(bad.outcome, .failed(7))
        XCTAssertTrue(bad.log.contains("downloaded 1 / 4"))
        XCTAssertTrue(bad.status.contains("exit 7"))
    }

    private func executableScript(_ url: URL, body: String) throws -> String {
        try "#!/bin/sh\n\(body)".write(to: url, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: url.path)
        return url.path
    }
}

private final class RecordingRunner: GuestProcessRunning {
    var started = false

    func run(
        executable: String,
        arguments: [String],
        onOutput: @escaping (String) -> Void
    ) async -> Int32 {
        started = true
        onOutput("downloaded 0\n")
        return 0
    }
}
