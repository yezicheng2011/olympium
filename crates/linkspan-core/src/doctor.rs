// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use crate::config::Config;
use crate::model::HostPlatform;
use crate::paths::Layout;
use crate::probe::Probe;
use serde::Serialize;
use std::path::Path;

#[derive(Debug, Clone, Serialize)]
pub struct DoctorReport {
    pub version: &'static str,
    pub host: HostPlatform,
    pub config_path: String,
    pub catalog_path: String,
    pub engines: Vec<EngineStatus>,
}

#[derive(Debug, Clone, Serialize)]
pub struct EngineStatus {
    pub id: &'static str,
    pub name: &'static str,
    pub applicable: bool,
    pub state: &'static str,
    pub location: Option<String>,
    pub detail: String,
}

pub fn doctor(
    host: HostPlatform,
    config: &Config,
    probe: &dyn Probe,
    layout: &Layout,
) -> DoctorReport {
    let mut engines = Vec::new();
    engines.push(builtin(
        host,
        "native",
        "Native execution",
        true,
        "Runs programs this computer can start by itself.",
    ));
    engines.push(builtin(
        host,
        "deb",
        "Debian package extractor",
        true,
        "Reads gzip .deb packages itself. xz and zstd packages need dpkg-deb.",
    ));
    engines.push(tool_status(
        host,
        "dpkg-deb",
        "dpkg-deb",
        host == HostPlatform::Linux,
        probe,
        "",
        &["dpkg-deb"],
        "Extracts Debian packages that use xz or zstd.",
        "Not required for gzip .deb packages.",
    ));
    engines.push(tool_status(
        host,
        "rpm",
        "rpm2cpio",
        matches!(host, HostPlatform::Linux | HostPlatform::Android),
        probe,
        "",
        &["rpm2cpio"],
        "Extracts RPM packages.",
        "Install rpm2cpio to extract RPM packages.",
    ));
    engines.push(tool_status(
        host,
        "flatpak",
        "Flatpak",
        host == HostPlatform::Linux,
        probe,
        "",
        &["flatpak"],
        "Runs an already installed Flatpak.",
        "Install flatpak to run an already installed Flatpak. Linkspan does not download apps.",
    ));
    engines.push(tool_status(
        host,
        "snap",
        "snap",
        host == HostPlatform::Linux,
        probe,
        "",
        &["snap"],
        "Runs an already installed snap.",
        "Install snapd to run an already installed snap. Linkspan does not install snaps.",
    ));
    engines.push(tool_status(
        host,
        "wine",
        "Wine",
        matches!(host, HostPlatform::Linux | HostPlatform::MacOs),
        probe,
        &config.engines.windows.wine,
        &["wine64", "wine"],
        "Runs Windows executables and installers.",
        "Install Wine to run Windows programs. Linkspan does not ship a Windows license.",
    ));
    engines.push(tool_status(
        host,
        "proton",
        "Proton",
        matches!(host, HostPlatform::Linux | HostPlatform::MacOs),
        probe,
        &config.engines.windows.proton,
        &["proton"],
        "Optional alternative to Wine for Windows executables.",
        "Set engines.windows.proton if you want Proton instead of Wine.",
    ));
    engines.push(tool_status(
        host,
        "wsl",
        "WSL",
        host == HostPlatform::Windows,
        probe,
        "",
        &["wsl.exe", "wsl"],
        "Runs Linux programs inside Windows Subsystem for Linux.",
        "Install a WSL distribution. Linkspan does not install one.",
    ));
    engines.push(tool_status(
        host,
        "lima",
        "Lima",
        host == HostPlatform::MacOs,
        probe,
        "",
        &["limactl"],
        "Runs Linux programs in a Lima virtual machine.",
        "Install Lima, or set engines.lima.prefix. Linkspan does not download a Linux image.",
    ));
    engines.push(tool_status(
        host,
        "waydroid",
        "Waydroid",
        matches!(
            host,
            HostPlatform::Linux | HostPlatform::Windows | HostPlatform::MacOs
        ),
        probe,
        &config.engines.android.waydroid,
        &["waydroid"],
        "Runs Android APKs in a Waydroid container.",
        "Install Waydroid. Linkspan does not download an Android image.",
    ));
    engines.push(tool_status(
        host,
        "adb",
        "adb",
        matches!(
            host,
            HostPlatform::Linux | HostPlatform::Windows | HostPlatform::MacOs
        ),
        probe,
        &config.engines.android.adb,
        &["adb"],
        "Installs and starts APKs on a running emulator or device.",
        "Install Android platform-tools. Linkspan does not download a system image.",
    ));
    engines.push(status(
        "apple-native",
        "macOS apps",
        host == HostPlatform::MacOs,
        if host == HostPlatform::MacOs {
            "ready"
        } else {
            "not_applicable"
        },
        None,
        if host == HostPlatform::MacOs {
            "Opens Mac apps, disk images, and installer packages with macOS.".into()
        } else {
            "macOS software is not run on this computer.".into()
        },
    ));
    engines.push(tool_status(
        host,
        "ios-mac",
        "iOS on Mac",
        host == HostPlatform::MacOs,
        probe,
        "",
        &["xcrun"],
        "Opens installed iOS apps, and can launch a Simulator build you compiled.",
        "Xcode is only needed for Simulator builds. Linkspan does not install IPA files.",
    ));
    engines.push(hook_status(
        "linux-hook",
        "Linux runtime hook",
        host == HostPlatform::Android,
        &config.engines.linux_hook.prefix,
        "Command prefix for a Linux runtime you installed.",
    ));
    engines.push(hook_status(
        "windows-hook",
        "Windows runtime hook",
        host == HostPlatform::Android,
        &config.engines.windows_hook.prefix,
        "Command prefix for a Windows runtime you installed.",
    ));
    engines.push(status(
        "openharmony-native",
        "OpenHarmony apps",
        host == HostPlatform::OpenHarmony,
        if host == HostPlatform::OpenHarmony {
            "ready"
        } else {
            "not_applicable"
        },
        None,
        if host == HostPlatform::OpenHarmony {
            "Uses OpenHarmony's own installer. Unsigned packages need developer mode.".into()
        } else {
            "This computer is not an OpenHarmony device.".into()
        },
    ));
    let image = if config.engines.openharmony.image.is_empty() {
        None
    } else {
        Some(config.engines.openharmony.image.clone())
    };
    engines.push(status(
        "openharmony-runtime",
        "OpenHarmony runtime",
        host.is_desktop(),
        if !host.is_desktop() {
            "not_applicable"
        } else if image.as_ref().is_some_and(|path| Path::new(path).join("EDITION").is_file()) {
            "ready"
        } else {
            "not_published"
        },
        image,
        "Slim OpenHarmony image from its repository. It is not bundled, and a HarmonyOS image is refused.".into(),
    ));
    let slim_wine = layout.runtimes.join("wine").join("EDITION").is_file();
    engines.push(status(
        "wine-slim",
        "Slim Wine runtime",
        matches!(host, HostPlatform::Linux | HostPlatform::MacOs),
        if !matches!(host, HostPlatform::Linux | HostPlatform::MacOs) {
            "not_applicable"
        } else if slim_wine {
            "ready"
        } else {
            "not_published"
        },
        None,
        "Wine stays available for Windows programs. The slim Wine runtime is a separate download and is not used for OpenHarmony.".into(),
    ));
    DoctorReport {
        version: env!("CARGO_PKG_VERSION"),
        host,
        config_path: layout.config.display().to_string(),
        catalog_path: layout.catalog.display().to_string(),
        engines,
    }
}

fn builtin(
    host: HostPlatform,
    id: &'static str,
    name: &'static str,
    applicable: bool,
    detail: &str,
) -> EngineStatus {
    let _ = host;
    status(id, name, applicable, "ready", None, detail.to_string())
}

#[allow(clippy::too_many_arguments)]
fn tool_status(
    host: HostPlatform,
    id: &'static str,
    name: &'static str,
    applicable: bool,
    probe: &dyn Probe,
    configured: &str,
    names: &[&str],
    ready_detail: &str,
    missing_detail: &str,
) -> EngineStatus {
    if !applicable {
        return status(
            id,
            name,
            false,
            "not_applicable",
            None,
            format!("Not used on {host}."),
        );
    }
    if !configured.is_empty() {
        let exists = Path::new(configured).is_file();
        return status(
            id,
            name,
            true,
            if exists { "ready" } else { "missing" },
            Some(configured.to_string()),
            if exists {
                ready_detail.to_string()
            } else {
                format!("The configured path does not exist: {configured}")
            },
        );
    }
    if let Some(path) = names.iter().find_map(|name| probe.lookup(name)) {
        status(
            id,
            name,
            true,
            "ready",
            Some(path),
            ready_detail.to_string(),
        )
    } else {
        status(id, name, true, "missing", None, missing_detail.to_string())
    }
}

fn hook_status(
    id: &'static str,
    name: &'static str,
    applicable: bool,
    prefix: &[String],
    detail: &str,
) -> EngineStatus {
    if !applicable {
        return status(
            id,
            name,
            false,
            "not_applicable",
            None,
            "Not used on this computer.".into(),
        );
    }
    if prefix.is_empty() {
        status(
            id,
            name,
            true,
            "needs_config",
            None,
            format!("{detail} Nothing is configured yet."),
        )
    } else {
        status(
            id,
            name,
            true,
            "ready",
            Some(prefix.join(" ")),
            detail.to_string(),
        )
    }
}

fn status(
    id: &'static str,
    name: &'static str,
    applicable: bool,
    state: &'static str,
    location: Option<String>,
    detail: String,
) -> EngineStatus {
    EngineStatus {
        id,
        name,
        applicable,
        state,
        location,
        detail,
    }
}
