// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

package app.linkspan.protocol;

/**
 * Client-side copy of the refusal rule. The Rust engine is the authority.
 * This check stays in the Android app so a damaged compatibility file cannot
 * launch macOS or iOS software on a phone or tablet.
 */
public final class LaunchGate {
    private LaunchGate() {
    }

    public static boolean mustRefuse(String host, String guest, String level) {
        if ("refused".equals(level)) {
            return true;
        }
        boolean appleGuest = "macos".equals(guest) || "ios".equals(guest);
        if (appleGuest && !"macos".equals(host)) {
            return true;
        }
        if ("openharmony".equals(guest) && "android".equals(host)) {
            return true;
        }
        return "openharmony".equals(host) && !"openharmony".equals(guest);
    }

    public static String refusalMessage(String host) {
        return refusalMessage(host, "macos");
    }

    public static String refusalMessage(String host, String guest) {
        if ("openharmony".equals(guest) && "android".equals(host)) {
            return "Linkspan will not run OpenHarmony software on Android. "
                    + "Android is not an OpenHarmony runtime, and Linkspan does not translate a HAP into an Android app.";
        }
        if ("openharmony".equals(host) && !"openharmony".equals(guest)) {
            return "On an OpenHarmony device, Linkspan runs OpenHarmony software with the system itself. "
                    + "It does not run other guest systems there.";
        }
        return "Linkspan will not run macOS or iOS software on " + hostLabel(host)
                + ". Apple licenses those systems for Apple hardware. "
                + "Linkspan does not include an emulator, disk image, translation layer, or installer for them.";
    }

    public static String hostLabel(String host) {
        return switch (host) {
            case "android" -> "Android";
            case "windows" -> "Windows";
            case "linux" -> "Linux";
            case "macos" -> "macOS";
            case "openharmony" -> "OpenHarmony";
            default -> host;
        };
    }
}
