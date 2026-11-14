// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

import Foundation

/// Client-side copy of the refusal rule. The Rust engine is the authority.
/// A damaged compatibility file still cannot launch macOS or iOS software
/// on a non-Apple computer.
public enum LaunchGate {
    public static func mustRefuse(host: String, guest: String, level: String) -> Bool {
        if level == "refused" {
            return true
        }
        let appleGuest = guest == "macos" || guest == "ios"
        if appleGuest && host != "macos" {
            return true
        }
        if guest == "openharmony" && host == "android" {
            return true
        }
        return host == "openharmony" && guest != "openharmony"
    }

    public static func refusalMessage(host: String) -> String {
        refusalMessage(host: host, guest: "macos")
    }

    public static func refusalMessage(host: String, guest: String) -> String {
        if guest == "openharmony" && host == "android" {
            return "Olympium will not run OpenHarmony software on Android. Android is not an OpenHarmony runtime, and Olympium does not translate a HAP into an Android app."
        }
        if host == "openharmony" && guest != "openharmony" {
            return "On an OpenHarmony device, Olympium runs OpenHarmony software with the system itself. It does not run other guest systems there."
        }
        return "Olympium will not run macOS or iOS software on \(hostLabel(host)). Apple licenses those systems for Apple hardware. Olympium does not include an emulator, disk image, translation layer, or installer for them."
    }

    public static func hostLabel(_ host: String) -> String {
        switch host {
        case "android": return "Android"
        case "windows": return "Windows"
        case "linux": return "Linux"
        case "macos": return "macOS"
        case "openharmony": return "OpenHarmony"
        default: return host
        }
    }
}
