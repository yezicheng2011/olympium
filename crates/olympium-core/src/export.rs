// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use crate::error::Error;
use crate::messages::{self, Limit};
use crate::model::{GuestSystem, HostPlatform, SupportLevel};
use crate::policy::{self, Cell, FormatRow};
use serde::Serialize;
use serde_json::{json, Value};

#[derive(Debug, Serialize)]
pub struct ShareDocument {
    pub schema: u32,
    pub product: &'static str,
    pub license: &'static str,
    pub copyright: &'static str,
    pub hosts: [HostPlatform; 5],
    pub guests: [GuestSystem; 6],
    pub levels: Vec<LevelInfo>,
    pub cells: Vec<Cell>,
    pub formats: Vec<FormatRow>,
    pub limits: &'static [Limit],
    pub engines: Vec<EngineDoc>,
}

#[derive(Debug, Serialize)]
pub struct LevelInfo {
    pub id: SupportLevel,
    pub title: &'static str,
    pub summary: &'static str,
}

#[derive(Debug, Serialize)]
pub struct EngineDoc {
    pub id: &'static str,
    pub name: &'static str,
    pub summary: &'static str,
}

pub fn share_document() -> ShareDocument {
    ShareDocument {
        schema: 1,
        product: "Olympium",
        license: "Apache-2.0",
        copyright: "Copyright 2026 Ye Zicheng",
        hosts: HostPlatform::ALL,
        guests: GuestSystem::ALL,
        levels: vec![
            LevelInfo {
                id: SupportLevel::Ready,
                title: SupportLevel::Ready.title(),
                summary: "The launcher in this repository can run this on its own.",
            },
            LevelInfo {
                id: SupportLevel::Engine,
                title: SupportLevel::Engine.title(),
                summary: "An adapter calls an engine you install. The engine is not bundled.",
            },
            LevelInfo {
                id: SupportLevel::Limited,
                title: SupportLevel::Limited.title(),
                summary: "Some cases run. The rest are refused, with a reason.",
            },
            LevelInfo {
                id: SupportLevel::Scaffold,
                title: SupportLevel::Scaffold.title(),
                summary: "A hook exists. Nothing runs until you configure a runtime command.",
            },
            LevelInfo {
                id: SupportLevel::Refused,
                title: SupportLevel::Refused.title(),
                summary: "Olympium will not run this.",
            },
        ],
        cells: policy::matrix(),
        formats: policy::formats(),
        limits: messages::limits(),
        engines: engine_docs(),
    }
}

pub fn export_json() -> String {
    let mut text = serde_json::to_string_pretty(&share_document())
        .expect("the compatibility document is JSON-serializable");
    text.push('\n');
    text
}

pub fn envelope(
    ok: bool,
    command: &str,
    host: HostPlatform,
    data: Option<Value>,
    error: Option<Value>,
) -> Value {
    json!({
        "ok": ok,
        "command": command,
        "host": host,
        "data": data,
        "error": error,
    })
}

pub fn error_envelope(command: &str, host: HostPlatform, err: &Error) -> Value {
    envelope(false, command, host, None, Some(err.to_json()))
}

fn engine_docs() -> Vec<EngineDoc> {
    vec![
        EngineDoc {
            id: "native",
            name: "Native execution",
            summary: "Starts a program the host can already run.",
        },
        EngineDoc {
            id: "deb",
            name: "Debian packages",
            summary: "Extracts a .deb for the current user and runs a program inside it.",
        },
        EngineDoc {
            id: "rpm",
            name: "RPM packages",
            summary: "Extracts an RPM with rpm2cpio and runs a program inside it.",
        },
        EngineDoc {
            id: "flatpak",
            name: "Flatpak",
            summary: "Runs an already installed Flatpak by name.",
        },
        EngineDoc {
            id: "snap",
            name: "snap",
            summary: "Runs an already installed snap by name.",
        },
        EngineDoc {
            id: "wine",
            name: "Wine",
            summary: "Runs Windows executables and MSI installers.",
        },
        EngineDoc {
            id: "proton",
            name: "Proton",
            summary: "Runs Windows executables when you select Proton.",
        },
        EngineDoc {
            id: "wsl",
            name: "WSL",
            summary: "Runs Linux programs inside Windows Subsystem for Linux.",
        },
        EngineDoc {
            id: "lima",
            name: "Lima",
            summary: "Runs Linux programs in a Lima virtual machine on macOS.",
        },
        EngineDoc {
            id: "android-runtime",
            name: "Android runtime",
            summary: "Uses Waydroid or adb. Neither image nor Play is bundled.",
        },
        EngineDoc {
            id: "android-intent",
            name: "Android apps",
            summary: "Installs an APK or opens an installed app on Android.",
        },
        EngineDoc {
            id: "apple-native",
            name: "Mac apps",
            summary: "Opens Mac apps, disk images, and packages on a Mac.",
        },
        EngineDoc {
            id: "ios-mac",
            name: "iOS on Mac",
            summary: "Opens an installed iOS app, or a Simulator build you compiled.",
        },
        EngineDoc {
            id: "linux-hook",
            name: "Linux hook",
            summary: "A command prefix you configure on Android.",
        },
        EngineDoc {
            id: "windows-hook",
            name: "Windows hook",
            summary: "A command prefix you configure on Android.",
        },
        EngineDoc {
            id: "openharmony-native",
            name: "OpenHarmony apps",
            summary: "Installs a HAP with OpenHarmony's own installer on an OpenHarmony device.",
        },
        EngineDoc {
            id: "openharmony-runtime",
            name: "OpenHarmony runtime",
            summary: "Uses hdc against a real OpenHarmony system. The slim image is downloaded, not bundled.",
        },
    ]
}
