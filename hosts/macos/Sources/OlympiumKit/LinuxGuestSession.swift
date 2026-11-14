// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

import Foundation

/// Decides whether the Mac window may download and boot the slim Linux guest.
/// The package does not install QEMU. CI installs QEMU on the runner; that
/// does not mean a user's Mac has it.
public struct LinuxGuestSession: Equatable {
    public let canBoot: Bool
    public let status: String
    public let qemuCommand: String
    public let qemuPath: String?
    public let arguments: [String]

    public static func qemuCommand(hostArchitecture: String) -> String {
        switch hostArchitecture {
        case "arm64", "aarch64":
            return "qemu-system-aarch64"
        case "x86_64":
            return "qemu-system-x86_64"
        default:
            return "qemu-system-\(hostArchitecture)"
        }
    }

    public static func evaluate(
        hostArchitecture: String,
        searchPaths: [String],
        isExecutable: (String) -> Bool
    ) -> LinuxGuestSession {
        let command = qemuCommand(hostArchitecture: hostArchitecture)
        for directory in searchPaths {
            let candidate = URL(fileURLWithPath: directory).appendingPathComponent(command).path
            if isExecutable(candidate) {
                return LinuxGuestSession(
                    canBoot: true,
                    status: "\(command) is \(candidate). This package does not install QEMU. Download, check, and boot uses the QEMU already on this Mac.",
                    qemuCommand: command,
                    qemuPath: candidate,
                    arguments: ["guest", "--guest", "linux"]
                )
            }
        }
        return LinuxGuestSession(
            canBoot: false,
            status: "\(command) was not found. This package does not include QEMU, so it cannot boot the guest yet. Install QEMU, for example with `brew install qemu`, and leave \(command) on PATH.",
            qemuCommand: command,
            qemuPath: nil,
            arguments: ["guest", "--guest", "linux"]
        )
    }
}

public enum LinuxGuestOutcome: Equatable {
    case blocked
    case finished
    case failed(Int32)
}

/// Shared by the Mac window and the automated harness. It does not click the UI.
public final class LinuxGuestController {
    public private(set) var session: LinuxGuestSession
    public private(set) var status: String
    public private(set) var log = ""
    public private(set) var busy = false
    public private(set) var outcome: LinuxGuestOutcome = .blocked
    private var runnerStarted = false

    public init(session: LinuxGuestSession) {
        self.session = session
        self.status = session.status
    }

    public var canBoot: Bool { session.canBoot }

    /// Starts the engine only when QEMU was found. Returns the exit code, or
    /// nil when boot was refused.
    @discardableResult
    public func start(
        enginePath: String?,
        isExecutable: (String) -> Bool,
        runner: GuestProcessRunning
    ) async -> Int32? {
        guard !busy else { return nil }
        guard session.canBoot else {
            outcome = .blocked
            status = session.status
            return nil
        }
        guard let enginePath, isExecutable(enginePath) else {
            outcome = .blocked
            status = "The Olympium engine is not in this app, so the guest cannot be downloaded."
            return nil
        }
        busy = true
        runnerStarted = true
        status = "Downloading the slim Linux guest. The checksum is checked before it is kept."
        log = ""
        let code = await runner.run(
            executable: enginePath,
            arguments: session.arguments,
            onOutput: { [weak self] text in
                self?.absorb(text)
            }
        )
        busy = false
        if code == 0 {
            outcome = .finished
            status = "The slim Linux guest command finished."
        } else {
            outcome = .failed(code)
            status = "The slim Linux guest command failed with exit \(code)."
        }
        return code
    }

    public func absorb(_ text: String) {
        log += text
        for line in text.split(whereSeparator: \.isNewline) where line.contains("downloaded") {
            status = String(line)
        }
    }

    public var didStartRunner: Bool { runnerStarted }
}

public protocol GuestProcessRunning {
    func run(
        executable: String,
        arguments: [String],
        onOutput: @escaping (String) -> Void
    ) async -> Int32
}

public struct FoundationGuestRunner: GuestProcessRunning {
    public init() {}

    public func run(
        executable: String,
        arguments: [String],
        onOutput: @escaping (String) -> Void
    ) async -> Int32 {
        await withCheckedContinuation { continuation in
            let process = Process()
            process.executableURL = URL(fileURLWithPath: executable)
            process.arguments = arguments
            var env = ProcessInfo.processInfo.environment
            let prefix = "/opt/homebrew/bin:/usr/local/bin"
            if let path = env["PATH"], !path.isEmpty {
                env["PATH"] = prefix + ":" + path
            } else {
                env["PATH"] = prefix + ":/usr/bin:/bin"
            }
            process.environment = env
            let output = Pipe()
            let errors = Pipe()
            process.standardOutput = output
            process.standardError = errors
            let lock = NSLock()
            func emit(_ data: Data) {
                guard !data.isEmpty, let text = String(data: data, encoding: .utf8), !text.isEmpty else {
                    return
                }
                lock.lock()
                onOutput(text)
                lock.unlock()
            }
            output.fileHandleForReading.readabilityHandler = { emit($0.availableData) }
            errors.fileHandleForReading.readabilityHandler = { emit($0.availableData) }
            process.terminationHandler = { proc in
                output.fileHandleForReading.readabilityHandler = nil
                errors.fileHandleForReading.readabilityHandler = nil
                emit(output.fileHandleForReading.readDataToEndOfFile())
                emit(errors.fileHandleForReading.readDataToEndOfFile())
                continuation.resume(returning: proc.terminationStatus)
            }
            do {
                try process.run()
            } catch {
                output.fileHandleForReading.readabilityHandler = nil
                errors.fileHandleForReading.readabilityHandler = nil
                continuation.resume(returning: 127)
            }
        }
    }
}
