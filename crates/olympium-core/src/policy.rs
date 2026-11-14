// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use crate::messages;
use crate::model::{GuestSystem, HostPlatform, PackageKind, SupportLevel};
use serde::Serialize;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Cell {
    pub host: HostPlatform,
    pub guest: GuestSystem,
    pub level: SupportLevel,
    pub engine_id: Option<&'static str>,
    pub title: &'static str,
    pub summary: &'static str,
    pub legal_note: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub struct FormatDecision {
    pub level: SupportLevel,
    pub summary: &'static str,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct FormatRow {
    pub kind: PackageKind,
    pub guest: GuestSystem,
    pub label: &'static str,
    pub hosts: Vec<FormatOnHost>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct FormatOnHost {
    pub host: HostPlatform,
    pub level: SupportLevel,
    pub summary: &'static str,
}

pub fn cell(host: HostPlatform, guest: GuestSystem) -> Cell {
    let level = cell_level(host, guest);
    Cell {
        host,
        guest,
        level,
        engine_id: cell_engine(host, guest),
        title: level.title(),
        summary: cell_summary(host, guest),
        legal_note: cell_legal(host, guest),
    }
}

pub fn matrix() -> Vec<Cell> {
    let mut cells = Vec::new();
    for host in HostPlatform::ALL {
        for guest in GuestSystem::ALL {
            cells.push(cell(host, guest));
        }
    }
    cells
}

pub fn matrix_table() -> String {
    let mut out = String::from("Host           ");
    for guest in GuestSystem::ALL {
        out.push_str(&format!("{:<14}", guest.as_str()));
    }
    out.push('\n');
    for host in HostPlatform::ALL {
        let mut line = format!("{:<15}", host.as_str());
        for guest in GuestSystem::ALL {
            line.push_str(&format!("{:<14}", cell(host, guest).level.as_str()));
        }
        out.push_str(line.trim_end());
        out.push('\n');
    }
    out
}

pub fn formats() -> Vec<FormatRow> {
    PackageKind::ALL
        .into_iter()
        .map(|kind| FormatRow {
            kind,
            guest: kind.guest(),
            label: kind.label(),
            hosts: HostPlatform::ALL
                .into_iter()
                .map(|host| {
                    let decision = format_decision(host, kind);
                    FormatOnHost {
                        host,
                        level: decision.level,
                        summary: decision.summary,
                    }
                })
                .collect(),
        })
        .collect()
}

pub fn cell_level(host: HostPlatform, guest: GuestSystem) -> SupportLevel {
    use GuestSystem as G;
    use HostPlatform as H;
    use SupportLevel::*;
    match (host, guest) {
        (_, G::MacOs | G::Ios) if host != H::MacOs => Refused,
        (H::Android, G::OpenHarmony) => Refused,
        (H::OpenHarmony, G::OpenHarmony) => Ready,
        (H::OpenHarmony, _) => Refused,
        (H::Windows | H::Linux | H::MacOs, G::OpenHarmony) => Limited,
        (H::Android, G::Android) => Ready,
        (H::Windows | H::Linux | H::MacOs, G::Android) => Engine,
        (H::Windows, G::Windows) => Ready,
        (H::Linux | H::MacOs, G::Windows) => Engine,
        (H::Android, G::Windows) => Scaffold,
        (H::Linux, G::Linux) => Ready,
        (H::Windows | H::MacOs, G::Linux) => Engine,
        (H::Android, G::Linux) => Scaffold,
        (H::MacOs, G::MacOs) => Ready,
        (H::MacOs, G::Ios) => Limited,
        _ => Refused,
    }
}

pub fn cell_engine(host: HostPlatform, guest: GuestSystem) -> Option<&'static str> {
    use GuestSystem as G;
    use HostPlatform as H;
    match (host, guest) {
        (H::Android, G::Android) => Some("android-intent"),
        (H::Windows | H::Linux | H::MacOs, G::Android) => Some("android-runtime"),
        (H::Windows, G::Windows) => Some("native"),
        (H::Linux | H::MacOs, G::Windows) => Some("wine"),
        (H::Android, G::Windows) => Some("windows-hook"),
        (H::Linux, G::Linux) => Some("native"),
        (H::Windows, G::Linux) => Some("wsl"),
        (H::MacOs, G::Linux) => Some("lima"),
        (H::Android, G::Linux) => Some("linux-hook"),
        (H::MacOs, G::MacOs) => Some("apple-native"),
        (H::MacOs, G::Ios) => Some("ios-mac"),
        (H::OpenHarmony, G::OpenHarmony) => Some("openharmony-native"),
        (H::Windows | H::Linux | H::MacOs, G::OpenHarmony) => Some("openharmony-runtime"),
        _ => None,
    }
}

pub fn cell_summary(host: HostPlatform, guest: GuestSystem) -> &'static str {
    use GuestSystem as G;
    use HostPlatform as H;
    match (host, guest) {
        (H::Android, G::Android) => {
            "Installs an APK you choose, using Android's own installer, or opens an app that is already installed."
        }
        (H::Windows | H::Linux | H::MacOs, G::Android) => {
            "Launches an APK with Waydroid or adb when you have already installed one of them. Olympium does not download an Android image or Google Play."
        }
        (H::Windows, G::Windows) => "Starts a Windows executable or installer directly on Windows.",
        (H::Linux | H::MacOs, G::Windows) => {
            "Runs a Windows executable or installer with Wine, kept for Windows guests, or with Proton when you select it. Wine opens native host windows. It is not used for OpenHarmony, and it is not bundled."
        }
        (H::Android, G::Windows) => {
            "A command prefix you configure can start a Windows program. Olympium does not include a Windows runtime for Android."
        }
        (H::Linux, G::Linux) => {
            "Runs Linux executables, scripts, gzip and system-tool .deb packages, and AppImages. RPM, Flatpak, and snap run when their tools are installed."
        }
        (H::Windows, G::Linux) => {
            "Runs Linux executables, scripts, AppImages, and .deb packages through WSL. Olympium does not install a distribution."
        }
        (H::MacOs, G::Linux) => {
            "Runs a script on macOS. Other Linux programs go through Lima, or a command prefix you set. The slim Alpine guest is downloaded with olympium fetch and booted with olympium guest; it is not a general Linux distribution."
        }
        (H::Android, G::Linux) => {
            "A command prefix you configure can start a Linux program. Olympium does not include a root filesystem."
        }
        (H::MacOs, G::MacOs) => "Opens a Mac app, disk image, installer package, or Mac program with macOS.",
        (H::MacOs, G::Ios) => messages::ios_on_mac_summary(),
        (_, G::MacOs | G::Ios) => {
            "Will not run. Apple licenses macOS and iOS for Apple hardware, and Olympium does not emulate them."
        }
        (H::Android, G::OpenHarmony) => {
            "Will not run. Android is not an OpenHarmony runtime, and Olympium does not translate a HAP into an Android app."
        }
        (H::OpenHarmony, G::OpenHarmony) => {
            "Installs and opens the package with OpenHarmony's own installer. Unsigned packages install only in developer mode."
        }
        (H::OpenHarmony, _) => messages::openharmony_host_only(),
        (H::Windows | H::Linux | H::MacOs, G::OpenHarmony) => {
            "Runs the package in a real OpenHarmony runtime you connect, or in the slim OpenHarmony image after it is downloaded. No HarmonyOS image is bundled."
        }
    }
}

pub fn cell_legal(host: HostPlatform, guest: GuestSystem) -> String {
    if guest.is_apple() && host != HostPlatform::MacOs {
        return messages::apple_refusal(host);
    }
    if host == HostPlatform::OpenHarmony && guest != GuestSystem::OpenHarmony {
        return messages::openharmony_host_only().to_string();
    }
    match guest {
        GuestSystem::Ios => messages::ios_on_mac_summary().to_string(),
        GuestSystem::Windows => messages::windows_note().to_string(),
        GuestSystem::Android => messages::play_note().to_string(),
        GuestSystem::OpenHarmony if host == HostPlatform::Android => {
            messages::openharmony_on_android(host)
        }
        GuestSystem::OpenHarmony if host == HostPlatform::OpenHarmony => {
            messages::openharmony_desktop_note().to_string()
        }
        GuestSystem::OpenHarmony => messages::openharmony_desktop_note().to_string(),
        GuestSystem::MacOs | GuestSystem::Linux => String::new(),
    }
}

pub fn format_decision(host: HostPlatform, kind: PackageKind) -> FormatDecision {
    use HostPlatform::*;
    use PackageKind::*;
    use SupportLevel::*;
    if kind.guest().is_apple() && host != MacOs {
        return FormatDecision {
            level: Refused,
            summary: "Not run on this host. Apple licenses macOS and iOS for Apple hardware, and Olympium does not emulate them.",
        };
    }
    match (host, kind) {
        (_, IosPackage) => FormatDecision {
            level: Refused,
            summary: "Olympium does not install IPA files or device app bundles, including on a Mac.",
        },
        (Android, AndroidApk | AndroidPackage) => FormatDecision {
            level: Ready,
            summary: "The Android app installs an APK you pick, or opens an app that is already installed.",
        },
        (Windows | Linux | MacOs, AndroidApk | AndroidPackage) => FormatDecision {
            level: Engine,
            summary: "Uses Waydroid or adb when one is already installed. No Android image is bundled.",
        },
        (Windows, WindowsExe) => FormatDecision {
            level: Ready,
            summary: "Starts the executable directly.",
        },
        (Windows, WindowsMsi) => FormatDecision {
            level: Ready,
            summary: "Starts the installer with Windows Installer.",
        },
        (Linux | MacOs, WindowsExe | WindowsMsi) => FormatDecision {
            level: Engine,
            summary: "Runs it with Wine, which is kept for Windows programs and opens a native host window, or Proton for an executable when you select it. Neither is a Windows license, and neither is used for OpenHarmony.",
        },
        (Android, WindowsExe | WindowsMsi) => FormatDecision {
            level: Scaffold,
            summary: "Runs only through a Windows runtime command you configure.",
        },
        (Linux, LinuxExecutable | LinuxScript | LinuxDeb | LinuxAppImage) => FormatDecision {
            level: Ready,
            summary: "Runs on this computer. A .deb is extracted for your user and is not installed system-wide.",
        },
        (Linux, LinuxRpm) => FormatDecision {
            level: Engine,
            summary: "Extracts the RPM with rpm2cpio, which is not bundled, and runs the program inside.",
        },
        (Linux, LinuxFlatpak) => FormatDecision {
            level: Engine,
            summary: "Runs an installed Flatpak by name. Olympium does not download Flatpak apps.",
        },
        (Linux, LinuxSnap) => FormatDecision {
            level: Engine,
            summary: "Runs an installed snap by name. Olympium does not install snaps.",
        },
        (Windows, LinuxExecutable | LinuxScript | LinuxAppImage | LinuxDeb) => FormatDecision {
            level: Engine,
            summary: "Runs the program through WSL. Olympium does not install a Linux distribution.",
        },
        (Windows, LinuxRpm | LinuxFlatpak | LinuxSnap) => FormatDecision {
            level: Scaffold,
            summary: "Recognized. On Windows, use a Linux executable, script, AppImage, or .deb, which can go through WSL.",
        },
        (MacOs, LinuxScript) => FormatDecision {
            level: Ready,
            summary: "Runs the script with macOS, not in a Linux virtual machine.",
        },
        (MacOs, LinuxExecutable | LinuxAppImage | LinuxDeb) => FormatDecision {
            level: Engine,
            summary: "Runs in a Lima virtual machine, or with a command prefix you set. No Linux image is bundled.",
        },
        (MacOs, LinuxRpm | LinuxFlatpak | LinuxSnap) => FormatDecision {
            level: Scaffold,
            summary: "Recognized, and not launched yet. An executable, script, AppImage, or .deb can use Lima.",
        },
        (Android, LinuxExecutable | LinuxScript | LinuxDeb | LinuxRpm | LinuxAppImage | LinuxFlatpak | LinuxSnap) => {
            FormatDecision {
                level: Scaffold,
                summary: "Runs only through a Linux runtime command you configure. No root filesystem is bundled.",
            }
        }
        (MacOs, MacOsApp | MacOsDmg | MacOsPkg | MacOsBinary) => FormatDecision {
            level: Ready,
            summary: "Opens it with macOS.",
        },
        (MacOs, IosBundle) => FormatDecision {
            level: Limited,
            summary: "Opens an iOS app that is already installed on this Mac.",
        },
        (MacOs, IosSimulatorApp) => FormatDecision {
            level: Limited,
            summary: "Launches a Simulator build you compiled with Xcode. This does not install an App Store app.",
        },
        (Android, OpenHarmonyHap | OpenHarmonyApp | OpenHarmonyBundle) => FormatDecision {
            level: Refused,
            summary: "Not run on Android. Olympium does not translate a HAP into an Android app.",
        },
        (OpenHarmony, OpenHarmonyHap | OpenHarmonyApp | OpenHarmonyBundle) => FormatDecision {
            level: Ready,
            summary: "Installs with OpenHarmony's own bundle installer. Unsigned packages install only in developer mode.",
        },
        (Windows | Linux | MacOs, OpenHarmonyHap | OpenHarmonyApp | OpenHarmonyBundle) => {
            FormatDecision {
                level: Limited,
                summary: "Runs in a real OpenHarmony runtime. The slim image is downloaded, not bundled. Unsigned packages need developer mode.",
            }
        }
        (OpenHarmony, _) => FormatDecision {
            level: Refused,
            summary: "This OpenHarmony client runs OpenHarmony software only.",
        },
        _ => FormatDecision {
            level: Refused,
            summary: "Olympium does not run this combination.",
        },
    }
}
