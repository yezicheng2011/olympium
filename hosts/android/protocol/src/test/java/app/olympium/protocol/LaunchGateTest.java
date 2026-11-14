// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

package app.olympium.protocol;

public final class LaunchGateTest {
    public static void main(String[] args) {
        check(LaunchGate.mustRefuse("linux", "macos", "ready"), "tampered macOS cell");
        check(LaunchGate.mustRefuse("android", "ios", "engine"), "tampered iOS cell");
        check(LaunchGate.mustRefuse("windows", "linux", "refused"), "explicit refusal");
        check(!LaunchGate.mustRefuse("macos", "macos", "ready"), "Mac apps on a Mac");
        check(!LaunchGate.mustRefuse("macos", "ios", "limited"), "limited iOS on a Mac");
        check(!LaunchGate.mustRefuse("android", "android", "ready"), "Android on Android");
        check(!LaunchGate.mustRefuse("linux", "windows", "engine"), "Windows on Linux");
        check(LaunchGate.mustRefuse("android", "openharmony", "ready"), "tampered OpenHarmony cell");
        check(LaunchGate.mustRefuse("openharmony", "android", "ready"), "other guest on OpenHarmony");
        check(!LaunchGate.mustRefuse("linux", "openharmony", "limited"), "OpenHarmony on Linux");
        check(!LaunchGate.mustRefuse("openharmony", "openharmony", "ready"), "OpenHarmony on itself");
        String message = LaunchGate.refusalMessage("android");
        check(message.contains("Olympium will not run macOS or iOS software"), "refusal text");
        check(message.contains("Android"), "host label");
        System.out.println("LaunchGateTest ok");
    }

    private static void check(boolean value, String label) {
        if (!value) {
            throw new AssertionError(label);
        }
    }
}
