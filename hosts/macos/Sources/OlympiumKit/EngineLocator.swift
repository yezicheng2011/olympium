// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

import Foundation

/// Finds the Olympium engine. A packaged Mac app keeps the binary in
/// `Contents/MacOS`, which is not on `PATH`.
public enum EngineLocator {
    public static func locate(environment: [String: String], bundledPath: String?) -> String? {
        for key in ["OLYMPIUM_CLI", "LINKSPAN_CLI"] {
            if let configured = environment[key], isExecutable(configured) {
                return configured
            }
        }
        if let bundledPath, isExecutable(bundledPath) {
            return bundledPath
        }
        let path = environment["PATH"] ?? ""
        for dir in path.split(separator: ":") {
            for name in ["olympium", "linkspan"] {
                let candidate = URL(fileURLWithPath: String(dir)).appendingPathComponent(name).path
                if isExecutable(candidate) {
                    return candidate
                }
            }
        }
        return nil
    }

    public static func bundledExecutable(bundleURL: URL) -> String {
        bundleURL
            .appendingPathComponent("Contents", isDirectory: true)
            .appendingPathComponent("MacOS", isDirectory: true)
            .appendingPathComponent("olympium")
            .path
    }

    private static func isExecutable(_ path: String) -> Bool {
        FileManager.default.isExecutableFile(atPath: path)
    }
}
