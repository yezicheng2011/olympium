// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use crate::error::Error;
use crate::messages;
use crate::model::HostPlatform;
use crate::paths::Layout;
use crate::util::lossy;
use std::collections::BTreeMap;
use std::fs;
use std::path::Path;
use std::process::Command;

/// Read the mount point from `hdiutil attach -plist` output.
pub fn parse_hdiutil_mount(bytes: &[u8]) -> Option<String> {
    let text = String::from_utf8_lossy(bytes);
    let key = "<key>mount-point</key>";
    let index = text.find(key)?;
    let rest = &text[index + key.len()..];
    let start = rest.find("<string>")? + "<string>".len();
    let end = start + rest[start..].find("</string>")?;
    let mount = rest[start..end].trim();
    if mount.is_empty() {
        None
    } else {
        Some(mount.to_string())
    }
}

pub fn open_disk_image(layout: &Layout, archive: &str) -> Result<(i32, String), Error> {
    require_mac()?;
    let output = Command::new("hdiutil")
        .args(["attach", "-nobrowse", "-readonly", "-plist"])
        .arg(archive)
        .output()
        .map_err(|err| Error::io(err, "hdiutil"))?;
    if !output.status.success() {
        return Err(Error::Failed {
            message: "macOS could not attach this disk image.".into(),
            guest_exit_code: output.status.code(),
            stdout: lossy(&output.stdout),
            stderr: lossy(&output.stderr),
        });
    }
    let mount = parse_hdiutil_mount(&output.stdout).ok_or_else(|| Error::Failed {
        message: "macOS attached the disk image but did not report a mount point.".into(),
        guest_exit_code: output.status.code(),
        stdout: lossy(&output.stdout),
        stderr: lossy(&output.stderr),
    })?;
    let item = first_openable(Path::new(&mount))?;
    let status = Command::new("open")
        .arg(&item)
        .status()
        .map_err(|err| Error::io(err, "open"))?;
    remember_mount(layout, archive, &mount)?;
    if !status.success() {
        return Err(Error::Failed {
            message: format!("macOS could not open {}.", item),
            guest_exit_code: status.code(),
            stdout: String::new(),
            stderr: String::new(),
        });
    }
    Ok((0, mount))
}

pub fn install_pkg(archive: &str) -> Result<i32, Error> {
    require_mac()?;
    let status = Command::new("installer")
        .args(["-pkg", archive, "-target", "CurrentUser"])
        .status()
        .map_err(|err| Error::io(err, "installer"))?;
    if status.success() {
        Ok(0)
    } else {
        Err(Error::Failed {
            message: "The macOS installer did not finish. Packages that modify the system may need an administrator, and Linkspan does not run them with extra privileges.".into(),
            guest_exit_code: status.code(),
            stdout: String::new(),
            stderr: String::new(),
        })
    }
}

pub fn eject(layout: &Layout, archive: &str) -> Result<String, Error> {
    require_mac()?;
    let mounts = read_mounts(layout)?;
    let Some(mount) = mounts.get(archive).cloned() else {
        return Err(Error::Target {
            message: "Linkspan does not have a mounted disk image for that program.".into(),
        });
    };
    let status = Command::new("hdiutil")
        .args(["detach", &mount])
        .status()
        .map_err(|err| Error::io(err, "hdiutil"))?;
    if !status.success() {
        return Err(Error::Failed {
            message: format!("macOS could not detach {mount}."),
            guest_exit_code: status.code(),
            stdout: String::new(),
            stderr: String::new(),
        });
    }
    let mut mounts = mounts;
    mounts.remove(archive);
    write_mounts(layout, &mounts)?;
    Ok(mount)
}

fn require_mac() -> Result<(), Error> {
    if HostPlatform::current() == HostPlatform::MacOs {
        Ok(())
    } else {
        Err(Error::Legal {
            id: "apple-platform-on-non-apple-hardware",
            message: messages::apple_refusal(HostPlatform::current()),
        })
    }
}

fn first_openable(mount: &Path) -> Result<String, Error> {
    let mut found = Vec::new();
    collect_openable(mount, 0, &mut found)?;
    found.sort();
    found.into_iter().next().ok_or_else(|| Error::Target {
        message: "The disk image does not contain a Mac app or installer package at the top level."
            .into(),
    })
}

fn collect_openable(dir: &Path, depth: usize, found: &mut Vec<String>) -> Result<(), Error> {
    if depth > 2 {
        return Ok(());
    }
    let entries = fs::read_dir(dir).map_err(|err| Error::io(err, dir.display()))?;
    for entry in entries.filter_map(|entry| entry.ok()) {
        let path = entry.path();
        let name = path
            .file_name()
            .map(|n| n.to_string_lossy().to_ascii_lowercase())
            .unwrap_or_default();
        if name.ends_with(".app") || name.ends_with(".pkg") || name.ends_with(".mpkg") {
            found.push(path.to_string_lossy().into_owned());
        } else if path.is_dir() && depth < 2 && !name.ends_with(".app") {
            collect_openable(&path, depth + 1, found)?;
        }
    }
    Ok(())
}

fn remember_mount(layout: &Layout, archive: &str, mount: &str) -> Result<(), Error> {
    let mut mounts = read_mounts(layout).unwrap_or_default();
    mounts.insert(archive.to_string(), mount.to_string());
    write_mounts(layout, &mounts)
}

fn read_mounts(layout: &Layout) -> Result<BTreeMap<String, String>, Error> {
    if !layout.mounts.exists() {
        return Ok(BTreeMap::new());
    }
    let text = fs::read_to_string(&layout.mounts)
        .map_err(|err| Error::io(err, layout.mounts.display()))?;
    serde_json::from_str(&text).map_err(|err| Error::Io {
        message: format!("could not read mounted disk images: {err}"),
    })
}

fn write_mounts(layout: &Layout, mounts: &BTreeMap<String, String>) -> Result<(), Error> {
    if let Some(parent) = layout.mounts.parent() {
        fs::create_dir_all(parent).map_err(|err| Error::io(err, parent.display()))?;
    }
    let text = serde_json::to_string_pretty(mounts).map_err(|err| Error::Io {
        message: format!("could not record the mounted disk image: {err}"),
    })?;
    fs::write(&layout.mounts, text).map_err(|err| Error::io(err, layout.mounts.display()))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_mount_point() {
        let plist = br#"<?xml version="1.0"?>
<plist><dict>
<key>system-entities</key>
<array><dict>
<key>mount-point</key>
<string>/Volumes/Example</string>
</dict></array>
</dict></plist>"#;
        assert_eq!(
            parse_hdiutil_mount(plist).as_deref(),
            Some("/Volumes/Example")
        );
    }
}
