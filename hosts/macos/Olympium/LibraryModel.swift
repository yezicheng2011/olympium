// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

import Combine
import Foundation
import OlympiumKit

@MainActor
final class LibraryModel: ObservableObject {
    @Published var engineLine = "Looking for the Olympium engine…"
    @Published var programStatus = "No programs yet."
    @Published var programs: [ProgramRow] = []
    @Published var cells: [CellRow] = []
    @Published var limits: [LimitNote] = []
    @Published var guestBusy = false
    @Published var guestStatus = "The slim Linux guest has not been started from this window."
    @Published var guestLog = ""
    private var guestProcess: Process?

    func load() {
        if let document = bundledDocument() ?? repositoryDocument() {
            cells = document.cells.map { cell in
                CellRow(
                    host: cell.host,
                    hostLabel: LaunchGate.hostLabel(cell.host),
                    guest: cell.guest,
                    level: cell.level,
                    title: cell.title,
                    summary: cell.summary
                )
            }
            limits = document.limits
        }
        if let cli = locateCli() {
            engineLine = "Engine: \(cli.path)"
            programs = listPrograms(cli)
        } else {
            engineLine = "Build the engine with cargo, then put olympium on PATH or set LINKSPAN_CLI. The compatibility list still comes from the copy shipped with this app."
            programStatus = "The engine is not available, so the library cannot be read."
        }
    }

    private func bundledDocument() -> CompatibilityDocument? {
        guard let url = Bundle.main.url(forResource: "compatibility", withExtension: "json"),
              let data = try? Data(contentsOf: url) else {
            return nil
        }
        return try? CompatibilityDocument.load(from: data)
    }

    private func repositoryDocument() -> CompatibilityDocument? {
        var url = URL(fileURLWithPath: #filePath)
        for _ in 0..<8 {
            url.deleteLastPathComponent()
            let candidate = url.appendingPathComponent("share/compatibility.json")
            if let data = try? Data(contentsOf: candidate),
               let document = try? CompatibilityDocument.load(from: data) {
                return document
            }
        }
        return nil
    }

    private func locateCli() -> URL? {
        let bundled = EngineLocator.bundledExecutable(bundleURL: Bundle.main.bundleURL)
        guard let path = EngineLocator.locate(
            environment: ProcessInfo.processInfo.environment,
            bundledPath: bundled
        ) else {
            return nil
        }
        return URL(fileURLWithPath: path)
    }

    private func listPrograms(_ cli: URL) -> [ProgramRow] {
        let process = Process()
        process.executableURL = cli
        process.arguments = ["--json", "list"]
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = Pipe()
        do {
            try process.run()
            process.waitUntilExit()
            let data = pipe.fileHandleForReading.readDataToEndOfFile()
            guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let payload = root["data"] as? [String: Any],
                  let programs = payload["programs"] as? [[String: Any]] else {
                programStatus = "The engine did not return a library."
                return []
            }
            if programs.isEmpty {
                programStatus = "No programs yet."
            }
            return programs.map { item in
                let id = item["id"] as? String ?? UUID().uuidString
                let name = item["name"] as? String ?? "Program"
                let guest = item["guest"] as? String ?? ""
                let level = cells.first { $0.host == "macos" && $0.guest == guest }?.level ?? "refused"
                let refused = LaunchGate.mustRefuse(host: "macos", guest: guest, level: level)
                let note = refused ? "Will not run on this Mac." : guest
                return ProgramRow(id: id, name: name, note: note)
            }
        } catch {
            programStatus = error.localizedDescription
            return []
        }
    }

    func downloadCheckAndBootLinuxGuest() {
        guard !guestBusy else { return }
        guard let cli = locateCli() else {
            guestStatus = "The Olympium engine is not in this app, so the guest cannot be downloaded."
            return
        }
        guestBusy = true
        guestStatus = "Downloading the slim Linux guest. The checksum is checked before it is kept."
        guestLog = ""
        let process = Process()
        process.executableURL = cli
        process.arguments = ["guest", "--guest", "linux"]
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
        output.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            Task { @MainActor in
                self?.appendGuestOutput(data)
            }
        }
        errors.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            Task { @MainActor in
                self?.appendGuestOutput(data)
            }
        }
        process.terminationHandler = { [weak self] proc in
            output.fileHandleForReading.readabilityHandler = nil
            errors.fileHandleForReading.readabilityHandler = nil
            let code = proc.terminationStatus
            Task { @MainActor in
                guard let self else { return }
                self.guestProcess = nil
                self.guestBusy = false
                if code == 0 {
                    self.guestStatus = "The slim Linux guest finished. The command reported the guest marker."
                } else {
                    self.guestStatus = "The slim Linux guest command failed with exit \(code)."
                }
            }
        }
        do {
            guestProcess = process
            try process.run()
        } catch {
            guestProcess = nil
            guestBusy = false
            guestStatus = "Could not start the guest command. \(error.localizedDescription)"
        }
    }

    private func appendGuestOutput(_ data: Data) {
        guard !data.isEmpty, let text = String(data: data, encoding: .utf8), !text.isEmpty else {
            return
        }
        Task { @MainActor in
            self.guestLog += text
        }
    }
}
