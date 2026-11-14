// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use serde::{Deserialize, Serialize};
use std::fmt;

/// The computer Linkspan is running on. There is no iOS host.
/// OpenHarmony is a host. Commercial HarmonyOS images are not.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HostPlatform {
    Android,
    Windows,
    Linux,
    #[serde(rename = "macos")]
    MacOs,
    #[serde(rename = "openharmony")]
    OpenHarmony,
}

impl HostPlatform {
    pub const ALL: [HostPlatform; 5] = [
        HostPlatform::Android,
        HostPlatform::Windows,
        HostPlatform::Linux,
        HostPlatform::MacOs,
        HostPlatform::OpenHarmony,
    ];

    pub fn as_str(self) -> &'static str {
        match self {
            Self::Android => "android",
            Self::Windows => "windows",
            Self::Linux => "linux",
            Self::MacOs => "macos",
            Self::OpenHarmony => "openharmony",
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::Android => "Android",
            Self::Windows => "Windows",
            Self::Linux => "Linux",
            Self::MacOs => "macOS",
            Self::OpenHarmony => "OpenHarmony",
        }
    }

    pub fn parse(value: &str) -> Option<Self> {
        match value.trim().to_ascii_lowercase().as_str() {
            "android" => Some(Self::Android),
            "windows" | "win32" | "win" => Some(Self::Windows),
            "linux" => Some(Self::Linux),
            "macos" | "mac" | "osx" => Some(Self::MacOs),
            "openharmony" | "ohos" | "harmony" => Some(Self::OpenHarmony),
            _ => None,
        }
    }

    pub fn current() -> Self {
        if let Ok(value) = std::env::var("LINKSPAN_HOST") {
            if let Some(host) = Self::parse(&value) {
                return host;
            }
        }
        if cfg!(target_os = "android") {
            Self::Android
        } else if cfg!(target_os = "windows") {
            Self::Windows
        } else if cfg!(target_os = "macos") {
            Self::MacOs
        } else {
            Self::Linux
        }
    }

    pub fn is_desktop(self) -> bool {
        matches!(self, Self::Windows | Self::Linux | Self::MacOs)
    }
}

impl fmt::Display for HostPlatform {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.label())
    }
}

/// A system a program belongs to. iOS is a guest, never a host.
/// OpenHarmony is the open-source guest. Commercial HarmonyOS is not a guest.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum GuestSystem {
    Android,
    Windows,
    Linux,
    #[serde(rename = "macos")]
    MacOs,
    Ios,
    #[serde(rename = "openharmony")]
    OpenHarmony,
}

impl GuestSystem {
    pub const ALL: [GuestSystem; 6] = [
        GuestSystem::Android,
        GuestSystem::Windows,
        GuestSystem::Linux,
        GuestSystem::MacOs,
        GuestSystem::Ios,
        GuestSystem::OpenHarmony,
    ];

    pub fn as_str(self) -> &'static str {
        match self {
            Self::Android => "android",
            Self::Windows => "windows",
            Self::Linux => "linux",
            Self::MacOs => "macos",
            Self::Ios => "ios",
            Self::OpenHarmony => "openharmony",
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::Android => "Android",
            Self::Windows => "Windows",
            Self::Linux => "Linux",
            Self::MacOs => "macOS",
            Self::Ios => "iOS",
            Self::OpenHarmony => "OpenHarmony",
        }
    }

    pub fn parse(value: &str) -> Option<Self> {
        match value.trim().to_ascii_lowercase().as_str() {
            "android" => Some(Self::Android),
            "windows" | "win32" | "win" => Some(Self::Windows),
            "linux" => Some(Self::Linux),
            "macos" | "mac" | "osx" => Some(Self::MacOs),
            "ios" | "iphone" | "ipad" => Some(Self::Ios),
            "openharmony" | "ohos" | "harmony" => Some(Self::OpenHarmony),
            _ => None,
        }
    }

    /// Commercial HarmonyOS is rejected by callers. This only flags the name.
    pub fn is_commercial_harmony_name(value: &str) -> bool {
        let lower = value.trim().to_ascii_lowercase();
        lower == "harmonyos" || lower.contains("harmonyos")
    }

    pub fn is_apple(self) -> bool {
        matches!(self, Self::MacOs | Self::Ios)
    }
}

impl fmt::Display for GuestSystem {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.label())
    }
}

/// How a program is packaged. The kind decides the launcher, not the file name alone.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum PackageKind {
    AndroidApk,
    AndroidPackage,
    WindowsExe,
    WindowsMsi,
    LinuxExecutable,
    LinuxScript,
    LinuxDeb,
    LinuxRpm,
    LinuxAppImage,
    LinuxFlatpak,
    LinuxSnap,
    #[serde(rename = "macos_app")]
    MacOsApp,
    #[serde(rename = "macos_dmg")]
    MacOsDmg,
    #[serde(rename = "macos_pkg")]
    MacOsPkg,
    #[serde(rename = "macos_binary")]
    MacOsBinary,
    IosBundle,
    IosSimulatorApp,
    IosPackage,
    #[serde(rename = "openharmony_hap")]
    OpenHarmonyHap,
    #[serde(rename = "openharmony_app")]
    OpenHarmonyApp,
    #[serde(rename = "openharmony_bundle")]
    OpenHarmonyBundle,
}

impl PackageKind {
    pub const ALL: [PackageKind; 21] = [
        PackageKind::AndroidApk,
        PackageKind::AndroidPackage,
        PackageKind::WindowsExe,
        PackageKind::WindowsMsi,
        PackageKind::LinuxExecutable,
        PackageKind::LinuxScript,
        PackageKind::LinuxDeb,
        PackageKind::LinuxRpm,
        PackageKind::LinuxAppImage,
        PackageKind::LinuxFlatpak,
        PackageKind::LinuxSnap,
        PackageKind::MacOsApp,
        PackageKind::MacOsDmg,
        PackageKind::MacOsPkg,
        PackageKind::MacOsBinary,
        PackageKind::IosBundle,
        PackageKind::IosSimulatorApp,
        PackageKind::IosPackage,
        PackageKind::OpenHarmonyHap,
        PackageKind::OpenHarmonyApp,
        PackageKind::OpenHarmonyBundle,
    ];

    pub fn as_str(self) -> &'static str {
        match self {
            Self::AndroidApk => "android_apk",
            Self::AndroidPackage => "android_package",
            Self::WindowsExe => "windows_exe",
            Self::WindowsMsi => "windows_msi",
            Self::LinuxExecutable => "linux_executable",
            Self::LinuxScript => "linux_script",
            Self::LinuxDeb => "linux_deb",
            Self::LinuxRpm => "linux_rpm",
            Self::LinuxAppImage => "linux_appimage",
            Self::LinuxFlatpak => "linux_flatpak",
            Self::LinuxSnap => "linux_snap",
            Self::MacOsApp => "macos_app",
            Self::MacOsDmg => "macos_dmg",
            Self::MacOsPkg => "macos_pkg",
            Self::MacOsBinary => "macos_binary",
            Self::IosBundle => "ios_bundle",
            Self::IosSimulatorApp => "ios_simulator_app",
            Self::IosPackage => "ios_package",
            Self::OpenHarmonyHap => "openharmony_hap",
            Self::OpenHarmonyApp => "openharmony_app",
            Self::OpenHarmonyBundle => "openharmony_bundle",
        }
    }

    pub fn parse(value: &str) -> Option<Self> {
        Self::ALL
            .into_iter()
            .find(|kind| kind.as_str() == value.trim().to_ascii_lowercase().as_str())
    }

    pub fn guest(self) -> GuestSystem {
        match self {
            Self::AndroidApk | Self::AndroidPackage => GuestSystem::Android,
            Self::WindowsExe | Self::WindowsMsi => GuestSystem::Windows,
            Self::LinuxExecutable
            | Self::LinuxScript
            | Self::LinuxDeb
            | Self::LinuxRpm
            | Self::LinuxAppImage
            | Self::LinuxFlatpak
            | Self::LinuxSnap => GuestSystem::Linux,
            Self::MacOsApp | Self::MacOsDmg | Self::MacOsPkg | Self::MacOsBinary => {
                GuestSystem::MacOs
            }
            Self::IosBundle | Self::IosSimulatorApp | Self::IosPackage => GuestSystem::Ios,
            Self::OpenHarmonyHap | Self::OpenHarmonyApp | Self::OpenHarmonyBundle => {
                GuestSystem::OpenHarmony
            }
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::AndroidApk => "Android APK",
            Self::AndroidPackage => "installed Android app",
            Self::WindowsExe => "Windows executable",
            Self::WindowsMsi => "Windows installer",
            Self::LinuxExecutable => "Linux executable",
            Self::LinuxScript => "script",
            Self::LinuxDeb => "Debian package",
            Self::LinuxRpm => "RPM package",
            Self::LinuxAppImage => "AppImage",
            Self::LinuxFlatpak => "Flatpak",
            Self::LinuxSnap => "snap",
            Self::MacOsApp => "Mac app",
            Self::MacOsDmg => "macOS disk image",
            Self::MacOsPkg => "macOS installer package",
            Self::MacOsBinary => "macOS program",
            Self::IosBundle => "installed iOS app",
            Self::IosSimulatorApp => "iOS Simulator app",
            Self::IosPackage => "iOS app package",
            Self::OpenHarmonyHap => "OpenHarmony HAP",
            Self::OpenHarmonyApp => "OpenHarmony app bundle",
            Self::OpenHarmonyBundle => "installed OpenHarmony app",
        }
    }

    pub fn is_openharmony(self) -> bool {
        matches!(
            self,
            Self::OpenHarmonyHap | Self::OpenHarmonyApp | Self::OpenHarmonyBundle
        )
    }
}

impl fmt::Display for PackageKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.label())
    }
}

/// What Linkspan can do with a host and guest pair, or with one package format.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SupportLevel {
    /// The launcher in this repository can run it on that host.
    Ready,
    /// An adapter calls an external engine. The engine is not bundled.
    Engine,
    /// Some cases run. Others are refused, with a reason.
    Limited,
    /// A typed hook exists. Nothing runs until a runtime command is configured.
    Scaffold,
    /// Linkspan will not run this combination.
    Refused,
}

impl SupportLevel {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Ready => "ready",
            Self::Engine => "engine",
            Self::Limited => "limited",
            Self::Scaffold => "scaffold",
            Self::Refused => "refused",
        }
    }

    pub fn title(self) -> &'static str {
        match self {
            Self::Ready => "Runs here",
            Self::Engine => "Needs an engine you install",
            Self::Limited => "Only in some cases",
            Self::Scaffold => "Hook only",
            Self::Refused => "Will not run",
        }
    }
}

/// Where the program lives. File paths are stored as written by the caller.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Target {
    File {
        path: String,
    },
    AndroidPackage {
        name: String,
    },
    #[serde(rename = "openharmony_bundle")]
    OpenHarmonyBundle {
        bundle_name: String,
    },
    IosBundle {
        bundle_id: String,
    },
    Flatpak {
        reference: String,
    },
    Snap {
        name: String,
    },
}

impl Target {
    pub fn file_path(&self) -> Option<&str> {
        match self {
            Self::File { path } => Some(path),
            _ => None,
        }
    }

    pub fn describe(&self) -> String {
        match self {
            Self::File { path } => path.clone(),
            Self::AndroidPackage { name } => format!("Android package {name}"),
            Self::OpenHarmonyBundle { bundle_name } => {
                format!("OpenHarmony bundle {bundle_name}")
            }
            Self::IosBundle { bundle_id } => format!("iOS bundle {bundle_id}"),
            Self::Flatpak { reference } => format!("Flatpak {reference}"),
            Self::Snap { name } => format!("snap {name}"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Appearance {
    System,
    Light,
    Dark,
}

impl Default for Appearance {
    fn default() -> Self {
        Self::System
    }
}

impl Appearance {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::System => "system",
            Self::Light => "light",
            Self::Dark => "dark",
        }
    }

    pub fn parse(value: &str) -> Option<Self> {
        match value.trim().to_ascii_lowercase().as_str() {
            "system" => Some(Self::System),
            "light" => Some(Self::Light),
            "dark" => Some(Self::Dark),
            _ => None,
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::System => "Follow system",
            Self::Light => "Light",
            Self::Dark => "Dark",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ArchiveFormat {
    Deb,
    Rpm,
}
