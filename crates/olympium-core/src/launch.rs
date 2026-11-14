// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use crate::apple;
use crate::classify::{bundle_id_from_plist, pe_machine};
use crate::config::Config;
use crate::cpio;
use crate::deb;
use crate::error::Error;
use crate::messages;
use crate::model::{ArchiveFormat, GuestSystem, HostPlatform, PackageKind, Target};
use crate::openharmony::{self, OhIdentity, OhSource, SignatureState};
use crate::paths::Layout;
use crate::probe::Probe;
use crate::request::LaunchRequest;
use crate::util::{
    parent_string, read_prefix, valid_bundle_id, valid_dotted_id, windows_path_to_wsl,
};
use serde::Serialize;
use std::fs;
use std::hash::{Hash, Hasher};
use std::io::Read;
use std::path::Path;
use std::process::{Command, Stdio};
use std::thread;

const OUTPUT_LIMIT: usize = 64 * 1024;

#[derive(Debug, Clone, Serialize)]
pub struct Prepared {
    pub name: String,
    pub host: HostPlatform,
    pub guest: GuestSystem,
    pub kind: PackageKind,
    pub engine_id: &'static str,
    pub summary: String,
    pub notes: Vec<String>,
    pub required_host: Option<HostPlatform>,
    pub launch: LaunchKind,
    pub surface: SurfacePlan,
}

/// How the guest is shown on the host. A nested emulator window is not the default.
#[derive(Debug, Clone, Serialize)]
pub struct SurfacePlan {
    pub mode: &'static str,
    pub hides_runtime_chrome: bool,
    pub input: &'static str,
    pub appearance: &'static str,
    pub summary: &'static str,
}

#[derive(Debug, Clone, Serialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum LaunchKind {
    Process {
        program: String,
        args: Vec<String>,
        working_dir: Option<String>,
    },
    Sequence {
        steps: Vec<ProcessStep>,
    },
    ExtractThenRun {
        archive: String,
        format: ArchiveFormat,
        selector: Option<String>,
        runner: PayloadRunner,
        tool: Option<String>,
        args: Vec<String>,
    },
    AndroidIntent {
        package: String,
    },
    AndroidInstall {
        apk: String,
        package: Option<String>,
    },
    OpenDiskImage {
        path: String,
    },
    InstallPackage {
        path: String,
    },
}

#[derive(Debug, Clone, Serialize)]
pub struct ProcessStep {
    pub program: String,
    pub args: Vec<String>,
    pub working_dir: Option<String>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum PayloadRunner {
    Native,
    Wsl { distro: String },
    Lima { limactl: String, instance: String },
    Prefix { prefix: Vec<String> },
}

#[derive(Debug, Clone, Default)]
pub struct RunOptions {
    pub capture: bool,
    pub detach: bool,
}

#[derive(Debug, Clone, Serialize)]
pub struct Execution {
    pub engine_id: String,
    pub summary: String,
    pub notes: Vec<String>,
    pub guest_exit_code: Option<i32>,
    pub stdout: String,
    pub stderr: String,
    pub truncated: bool,
    pub detached_pid: Option<u32>,
}

pub fn prepare(
    host: HostPlatform,
    request: &LaunchRequest,
    config: &Config,
    probe: &dyn Probe,
) -> Result<Prepared, Error> {
    prepare_with(
        host,
        request,
        config,
        probe,
        &openharmony::SystemOpenHarmony,
    )
}

pub fn prepare_with(
    host: HostPlatform,
    request: &LaunchRequest,
    config: &Config,
    probe: &dyn Probe,
    openharmony_source: &dyn OhSource,
) -> Result<Prepared, Error> {
    validate_request(request)?;
    // Apple guests are refused before any command is built. Do not add an
    // emulator, translation layer, disk image, or installer behind this check.
    if (request.guest.is_apple() || request.kind.guest().is_apple()) && host != HostPlatform::MacOs
    {
        return Err(Error::Legal {
            id: "apple-platform-on-non-apple-hardware",
            message: messages::apple_refusal(host),
        });
    }
    if request.kind.guest() != request.guest {
        return Err(Error::Mismatch {
            message: format!(
                "This is a {}, which belongs to {}, not {}.",
                request.kind.label(),
                request.kind.guest().label(),
                request.guest.label()
            ),
        });
    }
    if request.kind == PackageKind::IosPackage {
        return Err(Error::Limited {
            id: "ios-package-not-installed",
            message: messages::ios_package_refusal(),
        });
    }
    match request.guest {
        GuestSystem::Android => prepare_android(host, request, config, probe),
        GuestSystem::Windows => prepare_windows(host, request, config, probe),
        GuestSystem::Linux => prepare_linux(host, request, config, probe),
        GuestSystem::MacOs => prepare_macos(request),
        GuestSystem::Ios => prepare_ios(request, probe),
        GuestSystem::OpenHarmony => {
            prepare_openharmony(host, request, config, probe, openharmony_source)
        }
    }
}

pub fn execute(
    prepared: &Prepared,
    options: &RunOptions,
    layout: &Layout,
) -> Result<Execution, Error> {
    if options.capture && options.detach {
        return Err(Error::Target {
            message: "Choose capture or detach, not both.".into(),
        });
    }
    if (prepared.guest.is_apple() || prepared.kind.guest().is_apple())
        && HostPlatform::current() != HostPlatform::MacOs
    {
        return Err(Error::Legal {
            id: "apple-platform-on-non-apple-hardware",
            message: messages::apple_refusal(HostPlatform::current()),
        });
    }
    if let Some(required) = prepared.required_host {
        if required != HostPlatform::current() {
            return Err(Error::WrongHost {
                message: format!("This launch plan runs only on {}.", required.label()),
            });
        }
    }
    match &prepared.launch {
        LaunchKind::Process {
            program,
            args,
            working_dir,
        } => {
            let step = ProcessStep {
                program: program.clone(),
                args: args.clone(),
                working_dir: working_dir.clone(),
            };
            finish_step(&step, options, prepared)
        }
        LaunchKind::Sequence { steps } => run_sequence(steps, options, prepared),
        LaunchKind::ExtractThenRun {
            archive,
            format,
            selector,
            runner,
            tool,
            args,
        } => execute_extract(
            archive, *format, selector.as_deref(), runner, tool.as_deref(), args, options, layout,
            prepared,
        ),
        LaunchKind::AndroidIntent { .. } | LaunchKind::AndroidInstall { .. } => {
            Err(Error::WrongHost {
                message: "The Android app installs or opens this program. The desktop engine cannot send an Android intent.".into(),
            })
        }
        LaunchKind::OpenDiskImage { path } => {
            let (_code, mount) = apple::open_disk_image(layout, path)?;
            Ok(success(
                prepared,
                Some(0),
                String::new(),
                format!("Mounted at {mount}\n"),
                false,
                None,
            ))
        }
        LaunchKind::InstallPackage { path } => {
            install_checked(path, prepared)?;
            Ok(success(prepared, Some(0), String::new(), String::new(), false, None))
        }
    }
}

fn install_checked(path: &str, prepared: &Prepared) -> Result<(), Error> {
    let _ = prepared;
    apple::install_pkg(path).map(|_| ())
}

fn validate_request(request: &LaunchRequest) -> Result<(), Error> {
    if request.name.trim().is_empty() {
        return Err(Error::Target {
            message: "The program needs a name.".into(),
        });
    }
    crate::util::reject_bad_text("name", &request.name)?;
    crate::util::reject_url(&request.name)?;
    if request.args.len() > 100 {
        return Err(Error::Target {
            message: "Too many arguments.".into(),
        });
    }
    for arg in &request.args {
        crate::util::reject_bad_text("argument", arg)?;
        if arg.len() > 32 * 1024 {
            return Err(Error::Target {
                message: "An argument is too long.".into(),
            });
        }
    }
    if let Some(selector) = &request.selector {
        crate::util::reject_bad_text("selector", selector)?;
        crate::util::reject_url(selector)?;
        if selector.starts_with('-') || selector.contains("..") || Path::new(selector).is_absolute()
        {
            return Err(Error::Target {
                message: "That program selector is not allowed.".into(),
            });
        }
        if matches!(
            request.kind,
            PackageKind::AndroidApk
                | PackageKind::AndroidPackage
                | PackageKind::OpenHarmonyHap
                | PackageKind::OpenHarmonyApp
                | PackageKind::OpenHarmonyBundle
        ) && !valid_dotted_id(selector)
        {
            return Err(Error::Target {
                message: format!("{selector} is not a package name."),
            });
        }
        if request.kind == PackageKind::IosSimulatorApp && !valid_bundle_id(selector) {
            return Err(Error::Target {
                message: format!("{selector} is not a bundle id."),
            });
        }
    }
    if let Some(path) = request.target.file_path() {
        crate::util::reject_bad_text("path", path)?;
        crate::util::reject_url(path)?;
    }
    Ok(())
}

fn prepare_android(
    host: HostPlatform,
    request: &LaunchRequest,
    config: &Config,
    probe: &dyn Probe,
) -> Result<Prepared, Error> {
    if host == HostPlatform::OpenHarmony {
        return Err(openharmony_host_refusal());
    }
    if host == HostPlatform::Android {
        return match request.kind {
            PackageKind::AndroidPackage => {
                let package = android_package_name(request)?;
                Ok(done(
                    request,
                    host,
                    "android-intent",
                    "Opens this installed Android app.",
                    notes(&[messages::play_note()]),
                    Some(HostPlatform::Android),
                    LaunchKind::AndroidIntent { package },
                ))
            }
            PackageKind::AndroidApk => {
                let apk = require_file(request)?.to_string();
                Ok(done(
                    request,
                    host,
                    "android-intent",
                    "Asks Android to install this APK. Android shows its own confirmation.",
                    notes(&[messages::play_note(), messages::no_download_note()]),
                    Some(HostPlatform::Android),
                    LaunchKind::AndroidInstall {
                        apk,
                        package: request.selector.clone(),
                    },
                ))
            }
            _ => Err(Error::Mismatch {
                message: "This is not an Android package.".into(),
            }),
        };
    }
    let tool = choose_android(config, probe)?;
    let summary = match &tool {
        AndroidTool::Waydroid(_) => "Uses Waydroid, which you installed separately.",
        AndroidTool::Adb(_) => "Uses adb, which you installed separately.",
    };
    let notes = notes(&[messages::play_note(), messages::no_download_note()]);
    match request.kind {
        PackageKind::AndroidPackage => {
            let package = android_package_name(request)?;
            let launch = match tool {
                AndroidTool::Waydroid(bin) => LaunchKind::Process {
                    program: bin,
                    args: vec!["app".into(), "launch".into(), package],
                    working_dir: None,
                },
                AndroidTool::Adb(bin) => LaunchKind::Process {
                    program: bin,
                    args: monkey_args(&package),
                    working_dir: None,
                },
            };
            Ok(done(
                request,
                host,
                "android-runtime",
                summary,
                notes,
                None,
                launch,
            ))
        }
        PackageKind::AndroidApk => {
            let apk = require_file(request)?.to_string();
            let package = request.selector.clone();
            let launch = match tool {
                AndroidTool::Waydroid(bin) => waydroid_steps(&bin, &apk, package.as_deref()),
                AndroidTool::Adb(bin) => adb_steps(&bin, &apk, package.as_deref()),
            };
            let summary = if package.is_some() {
                summary
            } else {
                "Installs the APK. It is not started, because no package name was given."
            };
            Ok(done(
                request,
                host,
                "android-runtime",
                summary,
                notes,
                None,
                launch,
            ))
        }
        _ => Err(Error::Mismatch {
            message: "This is not an Android package.".into(),
        }),
    }
}

fn prepare_windows(
    host: HostPlatform,
    request: &LaunchRequest,
    config: &Config,
    probe: &dyn Probe,
) -> Result<Prepared, Error> {
    if host == HostPlatform::OpenHarmony {
        return Err(openharmony_host_refusal());
    }
    let path = require_file(request)?;
    match host {
        HostPlatform::Windows => {
            let (program, args, summary) = if request.kind == PackageKind::WindowsMsi {
                let mut args = vec!["/i".into(), path.to_string()];
                args.extend(request.args.clone());
                (
                    "msiexec".to_string(),
                    args,
                    "Starts the installer with Windows Installer.",
                )
            } else {
                (
                    path.to_string(),
                    request.args.clone(),
                    "Starts the program directly.",
                )
            };
            Ok(done(
                request,
                host,
                "native",
                summary,
                notes(&[messages::windows_note()]),
                Some(HostPlatform::Windows),
                LaunchKind::Process {
                    program,
                    args,
                    working_dir: parent_string(path),
                },
            ))
        }
        HostPlatform::Linux | HostPlatform::MacOs => {
            let choice = choose_windows_engine(config, probe, Path::new(path), request.kind)?;
            let (engine_id, program, mut args, summary) = match choice {
                WindowsChoice::Wine(bin) if request.kind == PackageKind::WindowsMsi => (
                    "wine",
                    bin,
                    vec!["msiexec".into(), "/i".into(), path.to_string()],
                    "Runs this installer with Wine. Wine is kept for Windows programs and opens a native host window.",
                ),
                WindowsChoice::Wine(bin) => (
                    "wine",
                    bin,
                    vec![path.to_string()],
                    "Runs this program with Wine. Wine is kept for Windows programs and opens a native host window.",
                ),
                WindowsChoice::Proton(bin) => (
                    "proton",
                    bin,
                    vec!["run".into(), path.to_string()],
                    "Runs this program with Proton.",
                ),
            };
            args.extend(request.args.clone());
            Ok(done(
                request,
                host,
                engine_id,
                summary,
                notes(&[messages::windows_note(), messages::no_download_note()]),
                None,
                LaunchKind::Process {
                    program,
                    args,
                    working_dir: parent_string(path),
                },
            ))
        }
        HostPlatform::Android => {
            if config.engines.windows_hook.prefix.is_empty() {
                return Err(Error::NotConfigured {
                    engine: "windows-hook",
                    message: "Set engines.windows_hook.prefix to a Windows runtime you installed. Olympium does not include one, and it does not include a Windows license.".into(),
                });
            }
            Ok(prefix_file(
                request,
                host,
                "windows-hook",
                "Runs the command prefix you configured.",
                &config.engines.windows_hook.prefix,
                path,
            ))
        }
        HostPlatform::OpenHarmony => Err(openharmony_host_refusal()),
    }
}

fn prepare_linux(
    host: HostPlatform,
    request: &LaunchRequest,
    config: &Config,
    probe: &dyn Probe,
) -> Result<Prepared, Error> {
    if host == HostPlatform::OpenHarmony {
        return Err(openharmony_host_refusal());
    }
    match host {
        HostPlatform::Linux => prepare_linux_on_linux(request, probe),
        HostPlatform::Windows => prepare_linux_on_windows(request, config, probe),
        HostPlatform::MacOs => prepare_linux_on_macos(request, config, probe),
        HostPlatform::Android => prepare_linux_hook(request, config, probe),
        HostPlatform::OpenHarmony => Err(openharmony_host_refusal()),
    }
}

fn prepare_linux_on_linux(request: &LaunchRequest, probe: &dyn Probe) -> Result<Prepared, Error> {
    match request.kind {
        PackageKind::LinuxExecutable | PackageKind::LinuxScript | PackageKind::LinuxAppImage => {
            let path = require_file(request)?;
            let summary = match request.kind {
                PackageKind::LinuxAppImage => "Starts the AppImage as a program.",
                PackageKind::LinuxScript => "Runs the script on this computer.",
                _ => "Runs the program on this computer.",
            };
            Ok(done(
                request,
                HostPlatform::Linux,
                "native",
                summary,
                Vec::new(),
                Some(HostPlatform::Linux),
                LaunchKind::Process {
                    program: path.to_string(),
                    args: request.args.clone(),
                    working_dir: parent_string(path),
                },
            ))
        }
        PackageKind::LinuxDeb => Ok(extract_plan(
            request,
            HostPlatform::Linux,
            "deb",
            "Extracts the .deb for your user and runs the program inside it. It is not installed system-wide.",
            notes(&["The package is extracted into Olympium's cache."]),
            Some(HostPlatform::Linux),
            ArchiveFormat::Deb,
            PayloadRunner::Native,
            None,
        )?),
        PackageKind::LinuxRpm => {
            let tool = lookup_any(probe, &["rpm2cpio"]).ok_or_else(|| Error::EngineMissing {
                engine: "rpm",
                message: "Install rpm2cpio to extract RPM packages. Olympium does not bundle it.".into(),
            })?;
            Ok(extract_plan(
                request,
                HostPlatform::Linux,
                "rpm",
                "Extracts the RPM with rpm2cpio and runs the program inside it.",
                notes(&[messages::no_download_note()]),
                Some(HostPlatform::Linux),
                ArchiveFormat::Rpm,
                PayloadRunner::Native,
                Some(tool),
            )?)
        }
        PackageKind::LinuxFlatpak => prepare_flatpak(request, probe),
        PackageKind::LinuxSnap => prepare_snap(request, probe),
        _ => Err(Error::Mismatch {
            message: "This is not a Linux program.".into(),
        }),
    }
}

fn prepare_flatpak(request: &LaunchRequest, probe: &dyn Probe) -> Result<Prepared, Error> {
    match &request.target {
        Target::Flatpak { reference } => {
            let flatpak = lookup_any(probe, &["flatpak"]).ok_or_else(|| Error::EngineMissing {
                engine: "flatpak",
                message: "Install flatpak to run an installed Flatpak. Olympium does not download Flatpak apps.".into(),
            })?;
            let mut args = vec!["run".into(), reference.clone()];
            args.extend(request.args.clone());
            Ok(done(
                request,
                HostPlatform::Linux,
                "flatpak",
                "Runs an installed Flatpak.",
                notes(&[messages::no_download_note()]),
                Some(HostPlatform::Linux),
                LaunchKind::Process {
                    program: flatpak,
                    args,
                    working_dir: None,
                },
            ))
        }
        Target::File { .. } => Err(Error::NotConfigured {
            engine: "flatpak",
            message: "Olympium does not install or download Flatpak files. Install the app yourself, then add it with --flatpak and its name.".into(),
        }),
        _ => Err(Error::Target {
            message: "A Flatpak needs a name or a file.".into(),
        }),
    }
}

fn prepare_snap(request: &LaunchRequest, probe: &dyn Probe) -> Result<Prepared, Error> {
    match &request.target {
        Target::Snap { name } => {
            let snap = lookup_any(probe, &["snap"]).ok_or_else(|| Error::EngineMissing {
                engine: "snap",
                message: "Install snapd to run an installed snap. Olympium does not install snaps.".into(),
            })?;
            let mut args = vec!["run".into(), name.clone()];
            args.extend(request.args.clone());
            Ok(done(
                request,
                HostPlatform::Linux,
                "snap",
                "Runs an installed snap. snapd has to already be running.",
                notes(&[messages::no_download_note()]),
                Some(HostPlatform::Linux),
                LaunchKind::Process {
                    program: snap,
                    args,
                    working_dir: None,
                },
            ))
        }
        Target::File { .. } => Err(Error::NotConfigured {
            engine: "snap",
            message: "Olympium does not install snap packages, because that needs root. Install the snap yourself, then run it with --snap and its name.".into(),
        }),
        _ => Err(Error::Target {
            message: "A snap needs a name or a file.".into(),
        }),
    }
}

fn prepare_linux_on_windows(
    request: &LaunchRequest,
    config: &Config,
    probe: &dyn Probe,
) -> Result<Prepared, Error> {
    let distro = config.engines.wsl.distro.clone();
    let _wsl = lookup_any(probe, &["wsl.exe", "wsl"]).ok_or_else(|| Error::EngineMissing {
        engine: "wsl",
        message: "Install a WSL distribution to run Linux programs. Olympium does not install one."
            .into(),
    })?;
    match request.kind {
        PackageKind::LinuxExecutable | PackageKind::LinuxScript | PackageKind::LinuxAppImage => {
            let path = require_file(request)?.to_string();
            let wsl_path = windows_path_to_wsl(&path).ok_or_else(|| Error::Target {
                message: format!("WSL needs a Windows path such as C:\\Apps\\program. This path cannot be translated: {path}"),
            })?;
            let (program, mut args) = if request.kind == PackageKind::LinuxScript {
                ("/bin/sh".to_string(), vec![wsl_path])
            } else {
                (wsl_path, Vec::new())
            };
            args.extend(request.args.clone());
            Ok(done(
                request,
                HostPlatform::Windows,
                "wsl",
                "Runs this program inside WSL.",
                notes(&["Olympium does not install a Linux distribution."]),
                Some(HostPlatform::Windows),
                LaunchKind::Process {
                    program: "wsl.exe".into(),
                    args: wsl_args(&distro, &program, &args),
                    working_dir: None,
                },
            ))
        }
        PackageKind::LinuxDeb => Ok(extract_plan(
            request,
            HostPlatform::Windows,
            "wsl",
            "Extracts the .deb and runs the program inside WSL.",
            notes(&["Olympium does not install a Linux distribution."]),
            Some(HostPlatform::Windows),
            ArchiveFormat::Deb,
            PayloadRunner::Wsl { distro },
            None,
        )?),
        PackageKind::LinuxRpm | PackageKind::LinuxFlatpak | PackageKind::LinuxSnap => {
            Err(Error::NotConfigured {
                engine: "wsl",
                message: "On Windows, Olympium can pass a Linux executable, script, AppImage, or .deb to WSL. This package format is recognized and not launched.".into(),
            })
        }
        _ => Err(Error::Mismatch {
            message: "This is not a Linux program.".into(),
        }),
    }
}

fn prepare_linux_on_macos(
    request: &LaunchRequest,
    config: &Config,
    probe: &dyn Probe,
) -> Result<Prepared, Error> {
    if request.kind == PackageKind::LinuxScript {
        let path = require_file(request)?;
        return Ok(done(
            request,
            HostPlatform::MacOs,
            "native",
            "Runs this script with macOS. It is not sent to a Linux virtual machine.",
            Vec::new(),
            Some(HostPlatform::MacOs),
            LaunchKind::Process {
                program: path.to_string(),
                args: request.args.clone(),
                working_dir: parent_string(path),
            },
        ));
    }
    if matches!(
        request.kind,
        PackageKind::LinuxRpm | PackageKind::LinuxFlatpak | PackageKind::LinuxSnap
    ) {
        return Err(Error::NotConfigured {
            engine: "lima",
            message: "On a Mac, Olympium can run a Linux executable, AppImage, or .deb. This package format is recognized and not launched.".into(),
        });
    }
    let runner = lima_runner(config, probe)?;
    let summary = match &runner {
        PayloadRunner::Prefix { .. } => "Runs this program with the command prefix you set.",
        _ => "Runs this program in a Lima virtual machine you already created.",
    };
    match request.kind {
        PackageKind::LinuxExecutable | PackageKind::LinuxAppImage => {
            let path = require_file(request)?.to_string();
            let launch = match runner {
                PayloadRunner::Lima { limactl, instance } => LaunchKind::Process {
                    program: limactl,
                    args: {
                        let mut args = vec!["shell".into(), instance, path];
                        args.extend(request.args.clone());
                        args
                    },
                    working_dir: None,
                },
                PayloadRunner::Prefix { prefix } => prefix_launch(&prefix, &path, &request.args),
                PayloadRunner::Native | PayloadRunner::Wsl { .. } => {
                    return Err(Error::Io {
                        message: "internal Lima runner mismatch".into(),
                    })
                }
            };
            Ok(done(
                request,
                HostPlatform::MacOs,
                "lima",
                summary,
                notes(&["The slim Alpine guest is a separate download. An arbitrary Linux program still needs Lima or a command prefix."]),
                Some(HostPlatform::MacOs),
                launch,
            ))
        }
        PackageKind::LinuxDeb => Ok(extract_plan(
            request,
            HostPlatform::MacOs,
            "lima",
            "Extracts the .deb and runs the program in Lima, or with your command prefix.",
            notes(&["The slim Alpine guest is a separate download. An arbitrary Linux program still needs Lima or a command prefix."]),
            Some(HostPlatform::MacOs),
            ArchiveFormat::Deb,
            runner,
            None,
        )?),
        _ => Err(Error::Mismatch {
            message: "This is not a Linux program.".into(),
        }),
    }
}

fn prepare_linux_hook(
    request: &LaunchRequest,
    config: &Config,
    probe: &dyn Probe,
) -> Result<Prepared, Error> {
    if matches!(request.target, Target::Flatpak { .. } | Target::Snap { .. }) {
        return Err(Error::NotConfigured {
            engine: "linux-hook",
            message: "Flatpak and snap names run on Linux. On Android, configure a runtime command for a file on the device.".into(),
        });
    }
    if config.engines.linux_hook.prefix.is_empty() {
        return Err(Error::NotConfigured {
            engine: "linux-hook",
            message: "Set engines.linux_hook.prefix to a command that can start a Linux program. Olympium does not include a root filesystem.".into(),
        });
    }
    let path = require_file(request)?;
    let prefix = config.engines.linux_hook.prefix.clone();
    match request.kind {
        PackageKind::LinuxDeb => Ok(extract_plan(
            request,
            HostPlatform::Android,
            "linux-hook",
            "Extracts the .deb and passes the program to the command you configured.",
            notes(&["Olympium does not emulate Linux by itself."]),
            None,
            ArchiveFormat::Deb,
            PayloadRunner::Prefix { prefix },
            None,
        )?),
        PackageKind::LinuxRpm => {
            let tool = lookup_any(probe, &["rpm2cpio"]).ok_or_else(|| Error::EngineMissing {
                engine: "rpm",
                message:
                    "Extracting an RPM needs rpm2cpio before the runtime command can start it."
                        .into(),
            })?;
            Ok(extract_plan(
                request,
                HostPlatform::Android,
                "linux-hook",
                "Extracts the RPM and passes the program to the command you configured.",
                notes(&["Olympium does not emulate Linux by itself."]),
                None,
                ArchiveFormat::Rpm,
                PayloadRunner::Prefix { prefix },
                Some(tool),
            )?)
        }
        _ => Ok(prefix_file(
            request,
            HostPlatform::Android,
            "linux-hook",
            "Runs the command prefix you configured.",
            &prefix,
            path,
        )),
    }
}

fn prepare_macos(request: &LaunchRequest) -> Result<Prepared, Error> {
    let path = require_file(request)?;
    match request.kind {
        PackageKind::MacOsDmg => Ok(done(
            request,
            HostPlatform::MacOs,
            "apple-native",
            "Opens this disk image with macOS and starts the app or installer inside.",
            notes(&["The image stays mounted until you eject it."]),
            Some(HostPlatform::MacOs),
            LaunchKind::OpenDiskImage {
                path: path.to_string(),
            },
        )),
        PackageKind::MacOsPkg => Ok(done(
            request,
            HostPlatform::MacOs,
            "apple-native",
            "Runs this installer for the current user. Olympium does not grant it extra privileges.",
            Vec::new(),
            Some(HostPlatform::MacOs),
            LaunchKind::InstallPackage {
                path: path.to_string(),
            },
        )),
        PackageKind::MacOsBinary => Ok(done(
            request,
            HostPlatform::MacOs,
            "apple-native",
            "Runs this Mac program.",
            Vec::new(),
            Some(HostPlatform::MacOs),
            LaunchKind::Process {
                program: path.to_string(),
                args: request.args.clone(),
                working_dir: parent_string(path),
            },
        )),
        PackageKind::MacOsApp => Ok(done(
            request,
            HostPlatform::MacOs,
            "apple-native",
            "Opens this Mac app.",
            Vec::new(),
            Some(HostPlatform::MacOs),
            LaunchKind::Process {
                program: "open".into(),
                args: vec![path.to_string()],
                working_dir: None,
            },
        )),
        _ => Err(Error::Mismatch {
            message: "This is not a Mac program.".into(),
        }),
    }
}

fn prepare_ios(request: &LaunchRequest, probe: &dyn Probe) -> Result<Prepared, Error> {
    match request.kind {
        PackageKind::IosBundle => {
            let bundle_id = match &request.target {
                Target::IosBundle { bundle_id } => bundle_id.clone(),
                _ => {
                    return Err(Error::Target {
                        message: "An installed iOS app needs a bundle id.".into(),
                    })
                }
            };
            Ok(done(
                request,
                HostPlatform::MacOs,
                "ios-mac",
                "Opens an iOS app that is already installed on this Mac.",
                notes(&[messages::ios_on_mac_summary()]),
                Some(HostPlatform::MacOs),
                LaunchKind::Process {
                    program: "open".into(),
                    args: vec!["-b".into(), bundle_id],
                    working_dir: None,
                },
            ))
        }
        PackageKind::IosSimulatorApp => {
            let xcrun = lookup_any(probe, &["xcrun"]).ok_or_else(|| Error::EngineMissing {
                engine: "ios-mac",
                message: "Install Xcode and its command line tools to launch a Simulator build. Olympium does not download Xcode.".into(),
            })?;
            let app = require_file(request)?.to_string();
            let bundle_id = if let Some(selector) = &request.selector {
                selector.clone()
            } else {
                bundle_id_from_plist(Path::new(&app)).ok_or_else(|| Error::Target {
                    message: "Olympium could not read the bundle id. Pass --bundle-id. This has to be a Simulator build you compiled, not an App Store app.".into(),
                })?
            };
            let mut launch_args =
                vec!["simctl".into(), "launch".into(), "booted".into(), bundle_id];
            launch_args.extend(request.args.clone());
            Ok(done(
                request,
                HostPlatform::MacOs,
                "ios-mac",
                "Installs and launches a Simulator build you compiled.",
                notes(&[messages::ios_on_mac_summary(), messages::no_download_note()]),
                Some(HostPlatform::MacOs),
                LaunchKind::Sequence {
                    steps: vec![
                        ProcessStep {
                            program: xcrun.clone(),
                            args: vec!["simctl".into(), "install".into(), "booted".into(), app],
                            working_dir: None,
                        },
                        ProcessStep {
                            program: xcrun,
                            args: launch_args,
                            working_dir: None,
                        },
                    ],
                },
            ))
        }
        PackageKind::IosPackage => Err(Error::Limited {
            id: "ios-package-not-installed",
            message: messages::ios_package_refusal(),
        }),
        _ => Err(Error::Mismatch {
            message: "This is not an iOS app.".into(),
        }),
    }
}

#[allow(clippy::too_many_arguments)]
fn execute_extract(
    archive: &str,
    format: ArchiveFormat,
    selector: Option<&str>,
    runner: &PayloadRunner,
    tool: Option<&str>,
    args: &[String],
    options: &RunOptions,
    layout: &Layout,
    prepared: &Prepared,
) -> Result<Execution, Error> {
    fs::create_dir_all(&layout.cache).map_err(|err| Error::io(err, layout.cache.display()))?;
    let dest = extract_dest(layout, archive);
    let package_name = match format {
        ArchiveFormat::Deb => deb::extract_deb(Path::new(archive), &dest)?,
        ArchiveFormat::Rpm => {
            let tool = tool.ok_or_else(|| Error::EngineMissing {
                engine: "rpm",
                message: "rpm2cpio is required to extract this package.".into(),
            })?;
            cpio::extract_rpm(Path::new(archive), &dest, Path::new(tool))?;
            None
        }
    };
    let program = deb::find_payload(&dest, selector, package_name.as_deref())?;
    make_executable(&program)?;
    let program_text = program.to_string_lossy().into_owned();
    let step = match runner {
        PayloadRunner::Native => ProcessStep {
            program: program_text.clone(),
            args: args.to_vec(),
            working_dir: parent_string(&program_text),
        },
        PayloadRunner::Wsl { distro } => {
            let wsl_path = windows_path_to_wsl(&program_text).ok_or_else(|| Error::Target {
                message: format!("Could not express {program_text} as a WSL path."),
            })?;
            let script = looks_like_script(&program);
            let (inner, mut inner_args) = if script {
                ("/bin/sh".to_string(), vec![wsl_path])
            } else {
                (wsl_path, Vec::new())
            };
            inner_args.extend(args.iter().cloned());
            ProcessStep {
                program: "wsl.exe".into(),
                args: wsl_args(distro, &inner, &inner_args),
                working_dir: None,
            }
        }
        PayloadRunner::Lima { limactl, instance } => {
            let mut lima_args = vec!["shell".into(), instance.clone(), program_text];
            lima_args.extend(args.iter().cloned());
            ProcessStep {
                program: limactl.clone(),
                args: lima_args,
                working_dir: None,
            }
        }
        PayloadRunner::Prefix { prefix } => {
            if prefix.is_empty() {
                return Err(Error::NotConfigured {
                    engine: "linux-hook",
                    message: "The runtime command is empty.".into(),
                });
            }
            let mut prefixed = prefix[1..].to_vec();
            prefixed.push(program_text);
            prefixed.extend(args.iter().cloned());
            ProcessStep {
                program: prefix[0].clone(),
                args: prefixed,
                working_dir: None,
            }
        }
    };
    finish_step(&step, options, prepared)
}

fn finish_step(
    step: &ProcessStep,
    options: &RunOptions,
    prepared: &Prepared,
) -> Result<Execution, Error> {
    let captured = run_captured(step, options)?;
    if let Some(code) = captured.exit_code {
        if code != 0 {
            return Err(Error::Failed {
                message: format!("{} exited with status {code}.", prepared.name),
                guest_exit_code: Some(code),
                stdout: captured.stdout,
                stderr: captured.stderr,
            });
        }
    }
    Ok(success(
        prepared,
        captured.exit_code,
        captured.stdout,
        captured.stderr,
        captured.truncated,
        captured.pid,
    ))
}

fn run_sequence(
    steps: &[ProcessStep],
    options: &RunOptions,
    prepared: &Prepared,
) -> Result<Execution, Error> {
    if options.detach {
        return Err(Error::Target {
            message: "This program has more than one step, so it cannot be detached.".into(),
        });
    }
    if steps.is_empty() {
        return Err(Error::Target {
            message: "This launch plan has no command.".into(),
        });
    }
    let mut stdout = String::new();
    let mut stderr = String::new();
    let mut truncated = false;
    for (index, step) in steps.iter().enumerate() {
        let captured = run_captured(step, options)?;
        append_output(&mut stdout, &captured.stdout);
        append_output(&mut stderr, &captured.stderr);
        truncated |= captured.truncated;
        if captured.exit_code.unwrap_or(0) != 0 {
            return Err(Error::Failed {
                message: format!("Step {} of {} failed.", index + 1, steps.len()),
                guest_exit_code: captured.exit_code,
                stdout,
                stderr,
            });
        }
    }
    Ok(success(prepared, Some(0), stdout, stderr, truncated, None))
}

struct Captured {
    exit_code: Option<i32>,
    stdout: String,
    stderr: String,
    truncated: bool,
    pid: Option<u32>,
}

fn run_captured(step: &ProcessStep, options: &RunOptions) -> Result<Captured, Error> {
    if !options.detach {
        ensure_user_file_executable(step)?;
    }
    let mut command = Command::new(&step.program);
    command.args(&step.args);
    if let Some(dir) = &step.working_dir {
        command.current_dir(dir);
    }
    if options.detach {
        command.stdin(Stdio::null());
        command.stdout(Stdio::inherit());
        command.stderr(Stdio::inherit());
        let child = command
            .spawn()
            .map_err(|err| spawn_error(&step.program, err))?;
        let pid = child.id();
        drop(child);
        return Ok(Captured {
            exit_code: None,
            stdout: String::new(),
            stderr: String::new(),
            truncated: false,
            pid: Some(pid),
        });
    }
    if !options.capture {
        command.stdin(Stdio::inherit());
        command.stdout(Stdio::inherit());
        command.stderr(Stdio::inherit());
        let status = command
            .status()
            .map_err(|err| spawn_error(&step.program, err))?;
        return Ok(Captured {
            exit_code: status.code(),
            stdout: String::new(),
            stderr: String::new(),
            truncated: false,
            pid: None,
        });
    }
    command.stdin(Stdio::null());
    command.stdout(Stdio::piped());
    command.stderr(Stdio::piped());
    let mut child = command
        .spawn()
        .map_err(|err| spawn_error(&step.program, err))?;
    let stdout = child.stdout.take();
    let stderr = child.stderr.take();
    let out_handle = thread::spawn(move || read_limited(stdout, OUTPUT_LIMIT));
    let err_handle = thread::spawn(move || read_limited(stderr, OUTPUT_LIMIT));
    let status = child.wait().map_err(|err| Error::io(err, &step.program))?;
    let (stdout, out_truncated) = out_handle.join().unwrap_or_else(|_| (String::new(), false));
    let (stderr, err_truncated) = err_handle.join().unwrap_or_else(|_| (String::new(), false));
    Ok(Captured {
        exit_code: status.code(),
        stdout,
        stderr,
        truncated: out_truncated || err_truncated,
        pid: None,
    })
}

fn ensure_user_file_executable(step: &ProcessStep) -> Result<(), Error> {
    #[cfg(unix)]
    {
        let path = Path::new(&step.program);
        if path.is_absolute() && path.is_file() {
            use std::os::unix::fs::PermissionsExt;
            let mode = fs::metadata(path)
                .map_err(|err| Error::io(err, path.display()))?
                .permissions()
                .mode();
            if mode & 0o111 == 0 && !step.program.ends_with("wsl.exe") {
                return Err(Error::Target {
                    message: format!(
                        "{} is not executable. Mark it executable, then run it again. Olympium does not change that file's permissions.",
                        path.display()
                    ),
                });
            }
        }
    }
    Ok(())
}

fn make_executable(path: &Path) -> Result<(), Error> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let meta = fs::symlink_metadata(path).map_err(|err| Error::io(err, path.display()))?;
        let mut perms = meta.permissions();
        let mode = perms.mode();
        if mode & 0o111 == 0 {
            perms.set_mode(mode | 0o755);
            fs::set_permissions(path, perms).map_err(|err| Error::io(err, path.display()))?;
        }
    }
    let _ = path;
    Ok(())
}

fn looks_like_script(path: &Path) -> bool {
    read_prefix(path, 2)
        .map(|bytes| bytes.len() >= 2 && bytes[0] == b'#' && bytes[1] == b'!')
        .unwrap_or(false)
}

fn read_limited<R: Read>(reader: Option<R>, limit: usize) -> (String, bool) {
    let Some(mut reader) = reader else {
        return (String::new(), false);
    };
    let mut kept = Vec::new();
    let mut tmp = [0u8; 8192];
    let mut truncated = false;
    loop {
        match reader.read(&mut tmp) {
            Ok(0) => break,
            Ok(size) => {
                if kept.len() >= limit {
                    truncated = true;
                    continue;
                }
                let room = limit - kept.len();
                let take = size.min(room);
                kept.extend_from_slice(&tmp[..take]);
                if take < size {
                    truncated = true;
                }
            }
            Err(err) if err.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(_) => break,
        }
    }
    (String::from_utf8_lossy(&kept).into_owned(), truncated)
}

fn spawn_error(program: &str, err: std::io::Error) -> Error {
    if err.kind() == std::io::ErrorKind::NotFound {
        Error::EngineMissing {
            engine: "native",
            message: format!("Could not start {program}. The command was not found."),
        }
    } else if err.kind() == std::io::ErrorKind::PermissionDenied {
        Error::Target {
            message: format!("{program} is not executable. Mark it executable, then run it again."),
        }
    } else {
        Error::io(err, program)
    }
}

fn success(
    prepared: &Prepared,
    guest_exit_code: Option<i32>,
    stdout: String,
    stderr: String,
    truncated: bool,
    detached_pid: Option<u32>,
) -> Execution {
    Execution {
        engine_id: prepared.engine_id.to_string(),
        summary: prepared.summary.clone(),
        notes: prepared.notes.clone(),
        guest_exit_code,
        stdout,
        stderr,
        truncated,
        detached_pid,
    }
}

fn append_output(total: &mut String, part: &str) {
    if part.is_empty() {
        return;
    }
    if !total.is_empty() && !total.ends_with('\n') {
        total.push('\n');
    }
    total.push_str(part);
}

fn extract_dest(layout: &Layout, archive: &str) -> std::path::PathBuf {
    let mut hasher = std::collections::hash_map::DefaultHasher::new();
    archive.hash(&mut hasher);
    layout.cache.join(format!("{:016x}", hasher.finish()))
}

#[allow(clippy::too_many_arguments)]
fn extract_plan(
    request: &LaunchRequest,
    host: HostPlatform,
    engine_id: &'static str,
    summary: &str,
    notes: Vec<String>,
    required_host: Option<HostPlatform>,
    format: ArchiveFormat,
    runner: PayloadRunner,
    tool: Option<String>,
) -> Result<Prepared, Error> {
    let archive = require_file(request)?.to_string();
    Ok(done(
        request,
        host,
        engine_id,
        summary,
        notes,
        required_host,
        LaunchKind::ExtractThenRun {
            archive,
            format,
            selector: request.selector.clone(),
            runner,
            tool,
            args: request.args.clone(),
        },
    ))
}

fn prefix_file(
    request: &LaunchRequest,
    host: HostPlatform,
    engine_id: &'static str,
    summary: &str,
    prefix: &[String],
    file: &str,
) -> Prepared {
    done(
        request,
        host,
        engine_id,
        summary,
        notes(&["Olympium runs the command you configured. It does not emulate the other system itself."]),
        None,
        prefix_launch(prefix, file, &request.args),
    )
}

fn prefix_launch(prefix: &[String], file: &str, extra: &[String]) -> LaunchKind {
    let mut args = prefix[1..].to_vec();
    args.push(file.to_string());
    args.extend(extra.iter().cloned());
    LaunchKind::Process {
        program: prefix[0].clone(),
        args,
        working_dir: parent_string(file),
    }
}

fn lima_runner(config: &Config, probe: &dyn Probe) -> Result<PayloadRunner, Error> {
    if !config.engines.lima.prefix.is_empty() {
        return Ok(PayloadRunner::Prefix {
            prefix: config.engines.lima.prefix.clone(),
        });
    }
    if let Some(limactl) = lookup_any(probe, &["limactl"]) {
        return Ok(PayloadRunner::Lima {
            limactl,
            instance: config.engines.lima.instance.clone(),
        });
    }
    Err(Error::NotConfigured {
        engine: "lima",
        message: "Install Lima and create an instance, or set engines.lima.prefix, to run an arbitrary Linux program. The slim Alpine guest is booted with `olympium guest --guest linux` after `olympium fetch --guest linux`.".into(),
    })
}

fn wsl_args(distro: &str, program: &str, args: &[String]) -> Vec<String> {
    let mut out = Vec::new();
    if !distro.is_empty() {
        out.push("-d".into());
        out.push(distro.to_string());
    }
    out.push("--exec".into());
    out.push(program.to_string());
    out.extend(args.iter().cloned());
    out
}

fn android_package_name(request: &LaunchRequest) -> Result<String, Error> {
    match &request.target {
        Target::AndroidPackage { name } if valid_dotted_id(name) => Ok(name.clone()),
        Target::AndroidPackage { name } => Err(Error::Target {
            message: format!("{name} is not an Android package name."),
        }),
        _ => Err(Error::Target {
            message: "An installed Android app needs a package name.".into(),
        }),
    }
}

fn require_file(request: &LaunchRequest) -> Result<&str, Error> {
    request.target.file_path().ok_or_else(|| Error::Target {
        message: "This action needs a file.".into(),
    })
}

enum AndroidTool {
    Waydroid(String),
    Adb(String),
}

fn choose_android(config: &Config, probe: &dyn Probe) -> Result<AndroidTool, Error> {
    let waydroid = resolve_optional(
        &config.engines.android.waydroid,
        probe,
        &["waydroid"],
        "waydroid",
    )?;
    let adb = resolve_optional(&config.engines.android.adb, probe, &["adb"], "adb")?;
    match config.engines.android.prefer.as_str() {
        "waydroid" => waydroid
            .map(AndroidTool::Waydroid)
            .ok_or_else(missing_android),
        "adb" => adb.map(AndroidTool::Adb).ok_or_else(missing_android),
        _ => {
            if let Some(path) = waydroid {
                Ok(AndroidTool::Waydroid(path))
            } else if let Some(path) = adb {
                Ok(AndroidTool::Adb(path))
            } else {
                Err(missing_android())
            }
        }
    }
}

fn missing_android() -> Error {
    Error::EngineMissing {
        engine: "android-runtime",
        message: "Install Waydroid or the Android SDK platform-tools (adb). Olympium does not download an Android system image or Google Play.".into(),
    }
}

fn waydroid_steps(bin: &str, apk: &str, package: Option<&str>) -> LaunchKind {
    let mut steps = vec![ProcessStep {
        program: bin.to_string(),
        args: vec!["app".into(), "install".into(), apk.to_string()],
        working_dir: None,
    }];
    if let Some(package) = package {
        steps.push(ProcessStep {
            program: bin.to_string(),
            args: vec!["app".into(), "launch".into(), package.to_string()],
            working_dir: None,
        });
    }
    LaunchKind::Sequence { steps }
}

fn adb_steps(bin: &str, apk: &str, package: Option<&str>) -> LaunchKind {
    let mut steps = vec![ProcessStep {
        program: bin.to_string(),
        args: vec!["install".into(), "-r".into(), apk.to_string()],
        working_dir: None,
    }];
    if let Some(package) = package {
        steps.push(ProcessStep {
            program: bin.to_string(),
            args: monkey_args(package),
            working_dir: None,
        });
    }
    LaunchKind::Sequence { steps }
}

fn monkey_args(package: &str) -> Vec<String> {
    vec![
        "shell".into(),
        "monkey".into(),
        "-p".into(),
        package.to_string(),
        "-c".into(),
        "android.intent.category.LAUNCHER".into(),
        "1".into(),
    ]
}

enum WindowsChoice {
    Wine(String),
    Proton(String),
}

fn choose_windows_engine(
    config: &Config,
    probe: &dyn Probe,
    file: &Path,
    kind: PackageKind,
) -> Result<WindowsChoice, Error> {
    if kind == PackageKind::WindowsMsi && config.engines.windows.prefer == "proton" {
        return Err(Error::EngineMissing {
            engine: "wine",
            message: "MSI installers need Wine. Proton is not used for them.".into(),
        });
    }
    let wine = choose_wine(config, probe, file)?;
    let proton = resolve_optional(&config.engines.windows.proton, probe, &["proton"], "proton")?;
    match config.engines.windows.prefer.as_str() {
        "proton" => proton.map(WindowsChoice::Proton).ok_or_else(missing_wine),
        "wine" => wine.map(WindowsChoice::Wine).ok_or_else(missing_wine),
        _ => {
            if let Some(path) = wine {
                Ok(WindowsChoice::Wine(path))
            } else if kind != PackageKind::WindowsMsi {
                proton.map(WindowsChoice::Proton).ok_or_else(missing_wine)
            } else {
                Err(missing_wine())
            }
        }
    }
}

fn choose_wine(config: &Config, probe: &dyn Probe, file: &Path) -> Result<Option<String>, Error> {
    if !config.engines.windows.wine.is_empty() {
        return resolve_optional(&config.engines.windows.wine, probe, &[], "wine");
    }
    let names: &[&str] = if pe_machine(file) == Some(0x014c) {
        &["wine", "wine64"]
    } else {
        &["wine64", "wine"]
    };
    Ok(lookup_any(probe, names))
}

fn missing_wine() -> Error {
    Error::EngineMissing {
        engine: "wine",
        message: "Install Wine, or download the slim Linux Wine runtime with `olympium fetch --guest windows`. That archive does not run on macOS. You can also set engines.windows.proton. Olympium does not ship a Windows license.".into(),
    }
}

fn resolve_optional(
    configured: &str,
    probe: &dyn Probe,
    names: &[&str],
    engine: &'static str,
) -> Result<Option<String>, Error> {
    if configured.is_empty() {
        return Ok(lookup_any(probe, names));
    }
    if Path::new(configured).is_file() {
        Ok(Some(configured.to_string()))
    } else {
        Err(Error::EngineMissing {
            engine,
            message: format!("The configured path for {engine} does not exist: {configured}"),
        })
    }
}

fn lookup_any(probe: &dyn Probe, names: &[&str]) -> Option<String> {
    names.iter().find_map(|name| probe.lookup(name))
}

fn done(
    request: &LaunchRequest,
    host: HostPlatform,
    engine_id: &'static str,
    summary: &str,
    notes: Vec<String>,
    required_host: Option<HostPlatform>,
    launch: LaunchKind,
) -> Prepared {
    Prepared {
        name: request.name.clone(),
        host,
        guest: request.guest,
        kind: request.kind,
        engine_id,
        summary: summary.to_string(),
        notes,
        required_host,
        launch,
        surface: surface_for(engine_id),
    }
}

fn surface_for(engine_id: &str) -> SurfacePlan {
    match engine_id {
        "wine" | "proton" => SurfacePlan {
            mode: "host_window",
            hides_runtime_chrome: true,
            input: "host",
            appearance: "follow_host",
            summary: "Wine keeps Windows programs in native host windows. It is not used for OpenHarmony.",
        },
        "android-runtime" | "waydroid" => SurfacePlan {
            mode: "host_window",
            hides_runtime_chrome: true,
            input: "host",
            appearance: "follow_host",
            summary: "The Android runtime shows the guest app in a host window.",
        },
        "openharmony-runtime" => SurfacePlan {
            mode: "host_window",
            hides_runtime_chrome: true,
            input: "host",
            appearance: "follow_host",
            summary: "The OpenHarmony host-window bridge shows one guest window. Olympium does not open a nested emulator frame.",
        },
        "openharmony-native" | "android-intent" | "apple-native" | "ios-mac" | "native" => {
            SurfacePlan {
                mode: "native",
                hides_runtime_chrome: true,
                input: "host",
                appearance: "follow_host",
                summary: "The program runs as a normal app on this computer.",
            }
        }
        "wsl" | "lima" => SurfacePlan {
            mode: "host_window",
            hides_runtime_chrome: true,
            input: "host",
            appearance: "follow_host",
            summary: "The guest runs in a real Linux environment and uses the host's window system when it has one.",
        },
        _ => SurfacePlan {
            mode: "native",
            hides_runtime_chrome: true,
            input: "host",
            appearance: "follow_host",
            summary: "The program is started directly.",
        },
    }
}

fn openharmony_host_refusal() -> Error {
    Error::Legal {
        id: "openharmony-host-native-only",
        message: messages::openharmony_host_only().to_string(),
    }
}

fn prepare_openharmony(
    host: HostPlatform,
    request: &LaunchRequest,
    config: &Config,
    probe: &dyn Probe,
    source: &dyn OhSource,
) -> Result<Prepared, Error> {
    if host == HostPlatform::Android {
        return Err(Error::Legal {
            id: "openharmony-on-android",
            message: messages::openharmony_on_android(host),
        });
    }
    if host == HostPlatform::OpenHarmony {
        return prepare_openharmony_native(request, config, probe, source);
    }
    if !host.is_desktop() {
        return Err(Error::Legal {
            id: "openharmony-on-android",
            message: messages::openharmony_on_android(host),
        });
    }
    let facts = source.facts(host, config, probe);
    match facts.identity {
        OhIdentity::CommercialHarmony => {
            return Err(Error::Legal {
                id: "commercial-harmonyos",
                message: messages::commercial_harmony_refusal().to_string(),
            });
        }
        OhIdentity::Other => {
            return Err(Error::Legal {
                id: "not-openharmony-runtime",
                message: "The connected runtime is not OpenHarmony. Olympium will not install a HAP into it, and it will not translate the package.".into(),
            });
        }
        OhIdentity::Unknown => {
            return Err(Error::NotConfigured {
                engine: "openharmony-runtime",
                message: "No OpenHarmony runtime is connected. Download the slim OpenHarmony image with `olympium fetch --guest openharmony` when one is published, or point engines.openharmony.hdc at an OpenHarmony device. Olympium does not bundle the image.".into(),
            });
        }
        OhIdentity::OpenHarmony => {}
    }
    let hdc = facts.hdc.clone().ok_or_else(|| Error::EngineMissing {
        engine: "openharmony-runtime",
        message: "hdc was not found. It comes with the slim OpenHarmony runtime, or you can set engines.openharmony.hdc. Olympium does not bundle it.".into(),
    })?;
    let info = package_info(request)?;
    let signature = info
        .as_ref()
        .map(|info| info.signature)
        .unwrap_or(SignatureState::Present);
    openharmony::allows_install(signature, facts.developer_mode)?;
    let bundle = launch_bundle(request, info.as_ref());
    let ability = launch_ability(info.as_ref());
    if facts.device_connected {
        return Ok(device_plan(
            request,
            host,
            &hdc,
            bundle.as_deref(),
            &ability,
        ));
    }
    let runtime = facts.runtime.clone().ok_or_else(|| Error::NotConfigured {
        engine: "openharmony-runtime",
        message: "The slim OpenHarmony image is missing bin/linkspan-runtime. Olympium will not start a nested emulator window in its place.".into(),
    })?;
    let bridge = facts.bridge.clone().ok_or_else(|| Error::NotConfigured {
        engine: "openharmony-runtime",
        message: "The slim OpenHarmony image is missing bin/linkspan-surface. Olympium will not open a nested emulator window instead of a host window.".into(),
    })?;
    Ok(local_image_plan(
        request,
        host,
        config,
        &hdc,
        &runtime,
        &bridge,
        bundle.as_deref(),
        &ability,
    ))
}

fn package_info(request: &LaunchRequest) -> Result<Option<openharmony::PackageInfo>, Error> {
    match request.kind {
        PackageKind::OpenHarmonyBundle => Ok(None),
        PackageKind::OpenHarmonyHap | PackageKind::OpenHarmonyApp => {
            let path = require_file(request)?;
            Ok(Some(openharmony::inspect_package(
                Path::new(path),
                request.kind,
            )?))
        }
        _ => Err(Error::Mismatch {
            message: "This is not an OpenHarmony package.".into(),
        }),
    }
}

fn launch_bundle(
    request: &LaunchRequest,
    info: Option<&openharmony::PackageInfo>,
) -> Option<String> {
    requested_bundle(request).or_else(|| info.and_then(|info| info.bundle_name.clone()))
}

fn launch_ability(info: Option<&openharmony::PackageInfo>) -> String {
    info.and_then(|info| info.ability.clone())
        .unwrap_or_else(|| "EntryAbility".into())
}

fn prepare_openharmony_native(
    request: &LaunchRequest,
    config: &Config,
    probe: &dyn Probe,
    source: &dyn OhSource,
) -> Result<Prepared, Error> {
    let _ = probe;
    let facts = source.facts(HostPlatform::OpenHarmony, config, probe);
    if facts.identity == OhIdentity::CommercialHarmony {
        return Err(Error::Legal {
            id: "commercial-harmonyos",
            message: messages::commercial_harmony_refusal().to_string(),
        });
    }
    let info = match request.kind {
        PackageKind::OpenHarmonyBundle => None,
        PackageKind::OpenHarmonyHap | PackageKind::OpenHarmonyApp => {
            let path = require_file(request)?;
            Some(openharmony::inspect_package(Path::new(path), request.kind)?)
        }
        _ => {
            return Err(Error::Mismatch {
                message: "This is not an OpenHarmony package.".into(),
            });
        }
    };
    let signature = info
        .as_ref()
        .map(|info| info.signature)
        .unwrap_or(SignatureState::Present);
    openharmony::allows_install(signature, facts.developer_mode)?;
    let bundle = requested_bundle(request)
        .or_else(|| info.as_ref().and_then(|info| info.bundle_name.clone()));
    let ability = info
        .as_ref()
        .and_then(|info| info.ability.clone())
        .unwrap_or_else(|| "EntryAbility".into());
    let mut steps = Vec::new();
    if let Some(path) = request.target.file_path() {
        steps.push(ProcessStep {
            program: "bm".into(),
            args: vec!["install".into(), "-p".into(), path.to_string()],
            working_dir: None,
        });
    }
    if let Some(bundle) = bundle {
        steps.push(ProcessStep {
            program: "aa".into(),
            args: vec!["start".into(), "-b".into(), bundle, "-a".into(), ability],
            working_dir: None,
        });
    }
    if steps.is_empty() {
        return Err(Error::Target {
            message: "An installed OpenHarmony app needs a bundle name.".into(),
        });
    }
    Ok(done(
        request,
        HostPlatform::OpenHarmony,
        "openharmony-native",
        "Installs or opens this with OpenHarmony. The system draws the window.",
        notes(&[messages::openharmony_desktop_note()]),
        Some(HostPlatform::OpenHarmony),
        LaunchKind::Sequence { steps },
    ))
}

fn device_plan(
    request: &LaunchRequest,
    host: HostPlatform,
    hdc: &str,
    bundle: Option<&str>,
    ability: &str,
) -> Prepared {
    let mut steps = Vec::new();
    if let Some(path) = request.target.file_path() {
        steps.push(ProcessStep {
            program: hdc.to_string(),
            args: vec!["install".into(), path.to_string()],
            working_dir: None,
        });
    }
    if let Some(bundle) = bundle {
        steps.push(ProcessStep {
            program: hdc.to_string(),
            args: vec![
                "shell".into(),
                "aa".into(),
                "start".into(),
                "-b".into(),
                bundle.to_string(),
                "-a".into(),
                ability.to_string(),
            ],
            working_dir: None,
        });
    }
    let launch = if steps.len() == 1 {
        let step = steps.remove(0);
        LaunchKind::Process {
            program: step.program,
            args: step.args,
            working_dir: step.working_dir,
        }
    } else {
        LaunchKind::Sequence { steps }
    };
    let mut prepared = done(
        request,
        host,
        "openharmony-runtime",
        "Installs the package with hdc on the OpenHarmony device you connected. The app opens on that device.",
        notes(&[
            messages::openharmony_desktop_note(),
            "The device's own screen is the window. Olympium does not open a nested emulator.",
        ]),
        None,
        launch,
    );
    prepared.surface = SurfacePlan {
        mode: "device",
        hides_runtime_chrome: true,
        input: "device",
        appearance: "follow_host",
        summary: "The OpenHarmony device shows the app. There is no nested emulator window.",
    };
    prepared
}

fn local_image_plan(
    request: &LaunchRequest,
    host: HostPlatform,
    config: &Config,
    hdc: &str,
    runtime: &str,
    bridge: &str,
    bundle: Option<&str>,
    ability: &str,
) -> Prepared {
    let memory = crate::runtime::memory_budget_mib(
        &config.engines.experience,
        crate::runtime::available_memory_mib(),
    );
    let mut start_args = vec![
        "start".into(),
        "--memory".into(),
        memory.to_string(),
        "--appearance".into(),
        config.appearance.as_str().into(),
        "--graphics".into(),
        config.engines.experience.graphics.clone(),
        "--camera".into(),
        config.engines.experience.camera.clone(),
        "--gamepad".into(),
        config.engines.experience.gamepad.clone(),
        "--display".into(),
        "host-window".into(),
        "--quick-boot".into(),
        if config.engines.experience.quick_boot {
            "on"
        } else {
            "off"
        }
        .into(),
    ];
    for folder in &config.engines.experience.shared_folders {
        start_args.push("--share".into());
        start_args.push(folder.clone());
    }
    let mut steps = vec![ProcessStep {
        program: runtime.to_string(),
        args: start_args,
        working_dir: None,
    }];
    if let Some(path) = request.target.file_path() {
        steps.push(ProcessStep {
            program: hdc.to_string(),
            args: vec!["install".into(), path.to_string()],
            working_dir: None,
        });
    }
    if let Some(bundle) = bundle {
        steps.push(ProcessStep {
            program: hdc.to_string(),
            args: vec![
                "shell".into(),
                "aa".into(),
                "start".into(),
                "-b".into(),
                bundle.to_string(),
                "-a".into(),
                ability.to_string(),
            ],
            working_dir: None,
        });
    }
    let mut bridge_args = vec![
        "--input".into(),
        "host".into(),
        "--appearance".into(),
        config.appearance.as_str().into(),
    ];
    if let Some(bundle) = bundle {
        bridge_args.push("--bundle".into());
        bridge_args.push(bundle.to_string());
    }
    steps.push(ProcessStep {
        program: bridge.to_string(),
        args: bridge_args,
        working_dir: None,
    });
    done(
        request,
        host,
        "openharmony-runtime",
        "Starts the slim OpenHarmony runtime without an emulator frame, installs the package with hdc, and opens a host window.",
        notes(&[messages::openharmony_desktop_note(), messages::no_download_note()]),
        None,
        LaunchKind::Sequence { steps },
    )
}

fn requested_bundle(request: &LaunchRequest) -> Option<String> {
    if let Some(selector) = request.selector.as_deref() {
        if !selector.is_empty() {
            return Some(selector.to_string());
        }
    }
    match &request.target {
        Target::OpenHarmonyBundle { bundle_name } => Some(bundle_name.clone()),
        _ => None,
    }
}

fn notes(items: &[&str]) -> Vec<String> {
    items.iter().map(|item| (*item).to_string()).collect()
}
