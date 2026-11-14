// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use crate::model::HostPlatform;
use serde::Serialize;

pub fn apple_refusal(host: HostPlatform) -> String {
    format!(
        "Olympium will not run macOS or iOS software on {host}. Apple licenses those systems for Apple hardware. Olympium does not include an emulator, disk image, translation layer, or installer for them. You can keep the file in the library."
    )
}

pub fn ios_package_refusal() -> String {
    "Olympium will not install this iOS app. It does not decrypt App Store packages, re-sign them, or sideload them. On a Mac, open an iOS app that is already installed, or run a Simulator build of an app you compiled with Xcode.".to_string()
}

pub fn ios_on_mac_summary() -> &'static str {
    "On a Mac, Olympium can open an iOS app that is already installed, and it can launch a Simulator build of an app you compiled with Xcode. It will not install an IPA or a device build."
}

pub fn windows_note() -> &'static str {
    "Wine is kept for Windows programs. It opens native host windows and is not a Windows license. Olympium does not use Wine for OpenHarmony, and it does not ship a Windows image, product key, or activation workaround."
}

pub fn openharmony_on_android(host: HostPlatform) -> String {
    format!(
        "Olympium will not run OpenHarmony software on {host}. Android is not an OpenHarmony runtime, and Olympium does not translate a HAP into an Android app. You can keep the file in the library. Linux, Windows, and macOS run a real OpenHarmony system from the slim image you download. An OpenHarmony device uses its own installer."
    )
}

pub fn openharmony_host_only() -> &'static str {
    "On an OpenHarmony device, Olympium runs OpenHarmony software with the system itself. It does not run Android, Windows, Linux, macOS, or iOS programs there, because that would be another system or a translation layer. Use the Linux, Windows, or macOS client for those guests."
}

pub fn openharmony_desktop_note() -> &'static str {
    "OpenHarmony packages run in a real OpenHarmony runtime: a device you connect, or the slim OpenHarmony image downloaded from its repository. Olympium does not bundle that image, does not use a HarmonyOS commercial image, and does not translate a HAP into another system. Unsigned packages install only in developer mode, through the official installer."
}

pub fn commercial_harmony_refusal() -> &'static str {
    "This runtime reports HarmonyOS, Huawei's commercial system. Olympium runs OpenHarmony only. It does not load a HarmonyOS image, Huawei keys, or a translation layer."
}

pub fn play_note() -> &'static str {
    "Olympium does not include Google Play, Play Services, or a Google account. This only uses an APK or package you already have the right to use."
}

pub fn no_download_note() -> &'static str {
    "Olympium does not download programs, system images, or store listings."
}

#[derive(Debug, Clone, Copy, Serialize)]
pub struct Limit {
    pub id: &'static str,
    pub title: &'static str,
    pub body: &'static str,
}

pub fn limits() -> &'static [Limit] {
    &[
        Limit {
            id: "apple-non-apple",
            title: "macOS and iOS on non-Apple computers",
            body: "Olympium will not run macOS or iOS software on non-Apple hardware. Apple licenses those systems for Apple hardware. Olympium does not include an emulator, disk image, translation layer, or installer for them, and it does not describe how to build one. A Mac or iOS file can stay in the library so you can see what it is.",
        },
        Limit {
            id: "ios-packages",
            title: "iOS packages",
            body: "Olympium will not install an IPA or a device .app bundle, including on a Mac. It does not decrypt App Store packages, remove DRM, or re-sign apps. On a Mac it can open an iOS app that is already installed, and it can launch a Simulator build of an app you compiled with Xcode.",
        },
        Limit {
            id: "no-ios-host",
            title: "No iOS app",
            body: "There is no iOS version of Olympium. The hosts are Android, Windows, Linux, macOS, and OpenHarmony. iOS software is only a guest, and only where the other limits on this page allow it.",
        },
        Limit {
            id: "windows-license",
            title: "Windows",
            body: "Wine is kept for Windows programs. It opens native host windows. It is not a Windows license, and it is not used for OpenHarmony. Olympium does not ship a Windows image, product key, or activation workaround. A slim Wine runtime can be downloaded from its own repository. A full Windows virtual machine needs an image and a license you already have.",
        },
        Limit {
            id: "play",
            title: "Google Play",
            body: "Olympium does not include Google Play, Play Services, or a Google sign-in. An Android program has to be an APK you already have, or a package already installed in your own Android runtime.",
        },
        Limit {
            id: "no-downloads",
            title: "Programs are not downloaded",
            body: "Olympium does not download applications, Apple system images, Flatpak runtimes, or snaps. Slim guest runtimes for Android, Linux, OpenHarmony, and Wine are downloaded when you open the app, from their own repositories, after the client IP chooses GitHub or a China mirror. Those images are not inside the Olympium program.",
        },
        Limit {
            id: "openharmony",
            title: "OpenHarmony, not HarmonyOS",
            body: "Harmony guests are OpenHarmony packages (.hap and OpenHarmony .app bundles). Olympium runs them on a real OpenHarmony system. It does not translate them into Android or Windows programs, and it does not ship a HarmonyOS commercial image, Huawei signing keys, or an unofficial port. Unsigned packages install only in developer mode through the official installer.",
        },
        Limit {
            id: "openharmony-host",
            title: "OpenHarmony host",
            body: "The OpenHarmony client runs OpenHarmony software on that device. It does not bundle guest images. Other guest systems are refused there. There is still no iOS host.",
        },
        Limit {
            id: "your-rights",
            title: "Your programs and the engines",
            body: "You are responsible for the rights to the programs you run. Wine, Proton, QEMU, Lima, Waydroid, WSL, OpenHarmony, GTK, and the system package tools stay under their own licenses. Olympium's own source is Apache-2.0 only.",
        },
    ]
}
