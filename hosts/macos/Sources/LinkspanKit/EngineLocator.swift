// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

import Foundation

/// Finds the Linkspan engine. A packaged Mac app keeps the binary in
/// `Contents/MacOS`, which is not on `PATH`.
public enum EngineLocator {
    public static func locate(environment: [String: String], bundledPath: String?) -> String? {
        if let configured = environment["LINKSPAN_CLI"], isExecutable(configured) {
            return configured
        }
        if let bundledPath, isExecutable(bundledPath) {
            return bundledPath
        }
        let path = environment["PATH"] ?? ""
        for dir in path.split(separator: ":") {
            let candidate = URL(fileURLWithPath: String(dir)).appendingPathComponent("linkspan").path
            if isExecutable(candidate) {
                return candidate
            }
        }
        return nil
    }

    public static func bundledExecutable(bundleURL: URL) -> String {
        bundleURL
            .appendingPathComponent("Contents", isDirectory: true)
            .appendingPathComponent("MacOS", isDirectory: true)
            .appendingPathComponent("linkspan")
            .path
    }

    private static func isExecutable(_ path: String) -> Bool {
        FileManager.default.isExecutableFile(atPath: path)
    }
}
