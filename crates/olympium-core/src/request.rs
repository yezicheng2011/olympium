// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use crate::classify::{classify_path, Classification};
use crate::error::Error;
use crate::model::{GuestSystem, PackageKind, Target};
use crate::util::{
    reject_bad_text, reject_url, valid_bundle_id, valid_dotted_id, valid_flatpak_ref,
    valid_snap_name,
};
use serde::Serialize;
use std::path::Path;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct LaunchRequest {
    pub name: String,
    pub guest: GuestSystem,
    pub kind: PackageKind,
    pub target: Target,
    pub args: Vec<String>,
    pub selector: Option<String>,
}

pub fn request_for_file(
    path: &Path,
    name: Option<&str>,
    explicit_kind: Option<PackageKind>,
    explicit_guest: Option<GuestSystem>,
    selector: Option<String>,
    args: Vec<String>,
) -> Result<LaunchRequest, Error> {
    reject_url(&path.to_string_lossy())?;
    if !path.exists() {
        return Err(Error::Target {
            message: format!("No file at {}.", path.display()),
        });
    }
    let canonical = path
        .canonicalize()
        .map_err(|err| Error::io(err, path.display()))?;
    let (kind, guest) = match classify_path(&canonical) {
        Ok(class) => resolve_identity(&class, explicit_kind, explicit_guest)?,
        Err(err) => match explicit_kind {
            Some(kind) if kind.guest().is_apple() || is_manual_kind(kind) => {
                if let Some(guest) = explicit_guest {
                    if guest != kind.guest() && !(kind.guest().is_apple() && guest.is_apple()) {
                        return Err(mismatch_guest(kind, guest));
                    }
                }
                (kind, kind.guest())
            }
            Some(_) => return Err(err),
            None => return Err(err),
        },
    };
    let display = name
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(str::to_string)
        .unwrap_or_else(|| default_name(&canonical));
    Ok(LaunchRequest {
        name: display,
        guest,
        kind,
        target: Target::File {
            path: canonical.to_string_lossy().into_owned(),
        },
        args,
        selector,
    })
}

pub fn request_for_android_package(
    name: &str,
    display: Option<&str>,
    args: Vec<String>,
) -> Result<LaunchRequest, Error> {
    let name = name.trim();
    reject_bad_text("package", name)?;
    reject_url(name)?;
    if !valid_dotted_id(name) {
        return Err(Error::Target {
            message: format!("{name} is not an Android package name like com.example.app."),
        });
    }
    Ok(LaunchRequest {
        name: display.unwrap_or(name).to_string(),
        guest: GuestSystem::Android,
        kind: PackageKind::AndroidPackage,
        target: Target::AndroidPackage {
            name: name.to_string(),
        },
        args,
        selector: None,
    })
}

pub fn request_for_openharmony_bundle(
    bundle_name: &str,
    display: Option<&str>,
    args: Vec<String>,
) -> Result<LaunchRequest, Error> {
    let bundle_name = bundle_name.trim();
    reject_bad_text("bundle", bundle_name)?;
    reject_url(bundle_name)?;
    if !valid_dotted_id(bundle_name) {
        return Err(Error::Target {
            message: format!(
                "{bundle_name} is not an OpenHarmony bundle name like com.example.app."
            ),
        });
    }
    Ok(LaunchRequest {
        name: display.unwrap_or(bundle_name).to_string(),
        guest: GuestSystem::OpenHarmony,
        kind: PackageKind::OpenHarmonyBundle,
        target: Target::OpenHarmonyBundle {
            bundle_name: bundle_name.to_string(),
        },
        args,
        selector: None,
    })
}

pub fn request_for_ios_bundle(
    bundle_id: &str,
    display: Option<&str>,
    args: Vec<String>,
) -> Result<LaunchRequest, Error> {
    let bundle_id = bundle_id.trim();
    reject_bad_text("bundle id", bundle_id)?;
    reject_url(bundle_id)?;
    if !valid_bundle_id(bundle_id) {
        return Err(Error::Target {
            message: format!("{bundle_id} is not an iOS bundle id."),
        });
    }
    Ok(LaunchRequest {
        name: display.unwrap_or(bundle_id).to_string(),
        guest: GuestSystem::Ios,
        kind: PackageKind::IosBundle,
        target: Target::IosBundle {
            bundle_id: bundle_id.to_string(),
        },
        args,
        selector: None,
    })
}

pub fn request_for_flatpak(
    reference: &str,
    display: Option<&str>,
    args: Vec<String>,
) -> Result<LaunchRequest, Error> {
    let reference = reference.trim();
    reject_bad_text("Flatpak ref", reference)?;
    reject_url(reference)?;
    if !valid_flatpak_ref(reference) {
        return Err(Error::Target {
            message: format!("{reference} is not a Flatpak name Olympium will run."),
        });
    }
    Ok(LaunchRequest {
        name: display.unwrap_or(reference).to_string(),
        guest: GuestSystem::Linux,
        kind: PackageKind::LinuxFlatpak,
        target: Target::Flatpak {
            reference: reference.to_string(),
        },
        args,
        selector: None,
    })
}

pub fn request_for_snap(
    name: &str,
    display: Option<&str>,
    args: Vec<String>,
) -> Result<LaunchRequest, Error> {
    let name = name.trim();
    reject_bad_text("snap", name)?;
    reject_url(name)?;
    if !valid_snap_name(name) {
        return Err(Error::Target {
            message: format!("{name} is not a snap name Olympium will run."),
        });
    }
    Ok(LaunchRequest {
        name: display.unwrap_or(name).to_string(),
        guest: GuestSystem::Linux,
        kind: PackageKind::LinuxSnap,
        target: Target::Snap {
            name: name.to_string(),
        },
        args,
        selector: None,
    })
}

pub fn resolve_identity(
    class: &Classification,
    explicit_kind: Option<PackageKind>,
    explicit_guest: Option<GuestSystem>,
) -> Result<(PackageKind, GuestSystem), Error> {
    if let Some(kind) = explicit_kind {
        if kind.guest().is_apple() && !class.kind.guest().is_apple() {
            return Err(Error::Mismatch {
                message: format!(
                    "This file is a {} and cannot be labeled as {}.",
                    class.kind.label(),
                    kind.label()
                ),
            });
        }
    }
    if class.kind.guest().is_apple() {
        if let Some(kind) = explicit_kind {
            if !kind.guest().is_apple() {
                return Err(Error::Mismatch {
                    message: format!(
                        "This file is {} and cannot be labeled as {}.",
                        class.guest.label(),
                        kind.guest().label()
                    ),
                });
            }
            if let Some(guest) = explicit_guest {
                if guest != kind.guest() {
                    return Err(mismatch_guest(kind, guest));
                }
            }
            return Ok((kind, kind.guest()));
        }
        if let Some(guest) = explicit_guest {
            if guest != class.guest {
                return Err(Error::Mismatch {
                    message: format!(
                        "This file is {} and cannot be labeled as {}.",
                        class.guest.label(),
                        guest.label()
                    ),
                });
            }
        }
        return Ok((class.kind, class.guest));
    }
    let kind = if let Some(kind) = explicit_kind {
        if class.confident && kind != class.kind {
            return Err(Error::Mismatch {
                message: format!(
                    "This file looks like a {} but was labeled as a {}.",
                    class.kind.label(),
                    kind.label()
                ),
            });
        }
        kind
    } else {
        class.kind
    };
    if let Some(guest) = explicit_guest {
        if guest != kind.guest() {
            return Err(mismatch_guest(kind, guest));
        }
    }
    Ok((kind, kind.guest()))
}

fn is_manual_kind(kind: PackageKind) -> bool {
    matches!(
        kind,
        PackageKind::LinuxExecutable
            | PackageKind::LinuxScript
            | PackageKind::WindowsExe
            | PackageKind::AndroidApk
    )
}

fn mismatch_guest(kind: PackageKind, guest: GuestSystem) -> Error {
    Error::Mismatch {
        message: format!(
            "A {} belongs to {}, not {}.",
            kind.label(),
            kind.guest().label(),
            guest.label()
        ),
    }
}

fn default_name(path: &Path) -> String {
    path.file_stem()
        .map(|stem| stem.to_string_lossy().into_owned())
        .filter(|name| !name.is_empty())
        .unwrap_or_else(|| "program".to_string())
}
