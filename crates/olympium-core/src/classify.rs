// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use crate::error::Error;
use crate::model::{GuestSystem, PackageKind};
use crate::util::{extension_lower, read_prefix};
use crate::zipdoc;
use serde::Serialize;
use std::path::Path;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Classification {
    pub guest: GuestSystem,
    pub kind: PackageKind,
    pub confident: bool,
    pub detail: String,
}

pub fn classify_path(path: &Path) -> Result<Classification, Error> {
    let meta = std::fs::metadata(path).map_err(|err| Error::io(err, path.display()))?;
    if meta.is_dir() {
        return classify_directory(path);
    }
    if !meta.is_file() {
        return Err(Error::Target {
            message: format!("{} is not a file.", path.display()),
        });
    }
    let bytes = read_prefix(path, 2 * 1024 * 1024)?;
    let ext = extension_lower(path);
    if is_macho(&bytes) {
        return Ok(classified(
            PackageKind::MacOsBinary,
            true,
            "Mach-O executable.",
        ));
    }
    if ext == "dmg" {
        return Ok(classified(PackageKind::MacOsDmg, true, "macOS disk image."));
    }
    if ext == "pkg" || ext == "mpkg" {
        return Ok(classified(
            PackageKind::MacOsPkg,
            true,
            "Treated as a macOS installer package.",
        ));
    }
    if let Some(found) = classify_zip_package(path, ext.as_str()) {
        return found;
    }
    let zip = is_zip(&bytes);
    let android_manifest = bytes_contain(&bytes, b"AndroidManifest.xml");
    let ios_payload = bytes_contain(&bytes, b"Payload/");
    if zip && android_manifest && ios_payload {
        return Err(Error::Target {
            message: format!(
                "{} looks like both an Android APK and an iOS package.",
                path.display()
            ),
        });
    }
    if ext == "ipa" || (zip && ios_payload && !android_manifest) {
        return Ok(classified(
            PackageKind::IosPackage,
            true,
            "iOS app package.",
        ));
    }
    if ext == "apk" || (zip && android_manifest) {
        return Ok(classified(PackageKind::AndroidApk, true, "Android APK."));
    }
    if ext == "deb" || is_deb_archive(&bytes) {
        return Ok(classified(PackageKind::LinuxDeb, true, "Debian package."));
    }
    if ext == "rpm" || is_rpm(&bytes) {
        return Ok(classified(PackageKind::LinuxRpm, true, "RPM package."));
    }
    if ext == "appimage" {
        return Ok(classified(PackageKind::LinuxAppImage, true, "AppImage."));
    }
    if ext == "flatpak" || ext == "flatpakref" {
        return Ok(classified(PackageKind::LinuxFlatpak, true, "Flatpak file."));
    }
    if ext == "snap" {
        return Ok(classified(PackageKind::LinuxSnap, true, "snap package."));
    }
    if ext == "msi" {
        return Ok(classified(
            PackageKind::WindowsMsi,
            true,
            "Windows installer.",
        ));
    }
    if is_elf(&bytes) {
        if looks_like_shared_library(path) {
            return Err(Error::Target {
                message: format!(
                    "{} looks like a shared library, not a program.",
                    path.display()
                ),
            });
        }
        if elf_type(&bytes) == Some(1) {
            return Err(Error::Target {
                message: format!(
                    "{} is a relocatable object file, not a program.",
                    path.display()
                ),
            });
        }
        let detail = if ext == "exe" {
            "The name ends in .exe, but the file is a Linux ELF program."
        } else {
            "Linux ELF program."
        };
        return Ok(classified(PackageKind::LinuxExecutable, true, detail));
    }
    if is_shebang(&bytes) {
        return Ok(classified(
            PackageKind::LinuxScript,
            true,
            "Script with a shebang.",
        ));
    }
    if is_mz(&bytes) || ext == "exe" || ext == "bat" || ext == "cmd" {
        return Ok(classified(
            PackageKind::WindowsExe,
            true,
            "Windows program.",
        ));
    }
    Err(Error::Target {
        message: format!(
            "Olympium does not recognize {}. If it is a Linux program, a Windows program, or an Android package, say so with --kind.",
            path.display()
        ),
    })
}

fn classify_zip_package(path: &Path, ext: &str) -> Option<Result<Classification, Error>> {
    let entries = match zipdoc::list_file(path) {
        Ok(entries) => entries,
        Err(_) if ext == "hap" => {
            return Some(Err(Error::Target {
                message: format!("{} is not an OpenHarmony HAP.", path.display()),
            }));
        }
        Err(_) => return None,
    };
    let names: Vec<&str> = entries.iter().map(|entry| entry.name.as_str()).collect();
    // Only the archive root is an OpenHarmony manifest. `assets/config.json`
    // is a normal file inside an APK or IPA.
    let has_android = names.iter().any(|name| *name == "AndroidManifest.xml");
    let has_root_module = names.iter().any(|name| *name == "module.json");
    let has_root_config = names.iter().any(|name| *name == "config.json");
    let has_module = has_root_module || (ext == "hap" && has_root_config);
    let has_pack = names.iter().any(|name| *name == "pack.info");
    let has_hap = names
        .iter()
        .any(|name| name.to_ascii_lowercase().ends_with(".hap"));
    if has_android && has_root_module {
        return Some(Err(Error::Target {
            message: format!(
                "{} looks like both an Android APK and an OpenHarmony package.",
                path.display()
            ),
        }));
    }
    if ext == "apk" || ext == "ipa" {
        return None;
    }
    if has_android && (has_module || ext == "hap") {
        return Some(Err(Error::Target {
            message: format!(
                "{} looks like both an Android APK and an OpenHarmony package.",
                path.display()
            ),
        }));
    }
    if ext == "hap" || (has_root_module && !has_android) {
        let detail = if has_module || has_root_module {
            "OpenHarmony HAP."
        } else {
            "Named as a HAP. No module.json was found."
        };
        return Some(Ok(classified(
            PackageKind::OpenHarmonyHap,
            has_root_module || has_module,
            detail,
        )));
    }
    if ext == "app" && has_pack && has_hap && !has_android {
        return Some(Ok(classified(
            PackageKind::OpenHarmonyApp,
            true,
            "OpenHarmony app bundle.",
        )));
    }
    None
}

fn classify_directory(path: &Path) -> Result<Classification, Error> {
    let name = path
        .file_name()
        .map(|n| n.to_string_lossy().to_string())
        .unwrap_or_default();
    if !name.to_ascii_lowercase().ends_with(".app") {
        return Err(Error::Target {
            message: format!(
                "{} is a directory, not a program Olympium recognizes.",
                path.display()
            ),
        });
    }
    let plist = read_plist_text(path);
    if plist_contains(&plist, "iphonesimulator") {
        return Ok(classified(
            PackageKind::IosSimulatorApp,
            true,
            "iOS Simulator app.",
        ));
    }
    if plist_contains(&plist, "iphoneos") {
        return Ok(classified(
            PackageKind::IosPackage,
            true,
            "iOS device app bundle.",
        ));
    }
    let detail = if plist.is_empty() {
        "Mac app bundle. No readable Info.plist was found."
    } else if plist.starts_with("bplist") {
        "Mac app bundle. The Info.plist is binary; pass --kind ios_simulator_app if it is a Simulator build."
    } else {
        "Mac app bundle."
    };
    let confident = !plist.starts_with("bplist");
    Ok(classified(PackageKind::MacOsApp, confident, detail))
}

fn read_plist_text(app: &Path) -> String {
    for relative in ["Contents/Info.plist", "Info.plist"] {
        let path = app.join(relative);
        if let Ok(bytes) = read_prefix(&path, 256 * 1024) {
            if bytes.starts_with(b"bplist") {
                return "bplist".to_string();
            }
            return String::from_utf8_lossy(&bytes).to_ascii_lowercase();
        }
    }
    String::new()
}

fn plist_contains(plist: &str, needle: &str) -> bool {
    plist.contains(needle)
}

fn classified(kind: PackageKind, confident: bool, detail: &str) -> Classification {
    Classification {
        guest: kind.guest(),
        kind,
        confident,
        detail: detail.to_string(),
    }
}

pub fn bundle_id_from_plist(app: &Path) -> Option<String> {
    let text = read_plist_text(app);
    let key = "<key>cfbundleidentifier</key>";
    let index = text.find(key)?;
    let rest = &text[index + key.len()..];
    let start = rest.find("<string>")? + "<string>".len();
    let end = rest[start..].find("</string>")? + start;
    let id = rest[start..end].trim();
    if crate::util::valid_bundle_id(id) {
        Some(id.to_string())
    } else {
        None
    }
}

fn is_elf(bytes: &[u8]) -> bool {
    bytes.len() >= 4 && bytes[0..4] == [0x7f, b'E', b'L', b'F']
}

fn elf_type(bytes: &[u8]) -> Option<u16> {
    if !is_elf(bytes) || bytes.len() < 18 {
        return None;
    }
    let little = bytes[5] == 1;
    Some(if little {
        u16::from_le_bytes([bytes[16], bytes[17]])
    } else {
        u16::from_be_bytes([bytes[16], bytes[17]])
    })
}

fn is_mz(bytes: &[u8]) -> bool {
    bytes.len() >= 2 && bytes[0..2] == *b"MZ"
}

fn is_rpm(bytes: &[u8]) -> bool {
    bytes.len() >= 4 && bytes[0..4] == [0xed, 0xab, 0xee, 0xdb]
}

fn is_zip(bytes: &[u8]) -> bool {
    bytes.len() >= 4
        && (bytes[0..4] == [0x50, 0x4b, 0x03, 0x04] || bytes[0..4] == [0x50, 0x4b, 0x05, 0x06])
}

fn is_deb_archive(bytes: &[u8]) -> bool {
    bytes.starts_with(b"!<arch>\n") && bytes_contain(bytes, b"debian-binary")
}

fn is_shebang(bytes: &[u8]) -> bool {
    bytes.len() >= 2 && bytes[0] == b'#' && bytes[1] == b'!'
}

fn looks_like_shared_library(path: &Path) -> bool {
    let name = path
        .file_name()
        .map(|n| n.to_string_lossy().to_ascii_lowercase())
        .unwrap_or_default();
    name.ends_with(".so") || name.contains(".so.")
}

pub fn is_macho(bytes: &[u8]) -> bool {
    if bytes.len() < 8 {
        return false;
    }
    let be = u32::from_be_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]);
    let le = u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]);
    const MH_MAGIC: u32 = 0xFEED_FACE;
    const MH_MAGIC_64: u32 = 0xFEED_FACF;
    if matches!(be, MH_MAGIC | MH_MAGIC_64) || matches!(le, MH_MAGIC | MH_MAGIC_64) {
        return true;
    }
    if be == 0xCAFE_BABE || be == 0xCAFE_BABF {
        let major = u16::from_be_bytes([bytes[6], bytes[7]]);
        if bytes[4] == 0 && (45..=80).contains(&major) && be == 0xCAFE_BABE {
            return false;
        }
        let nfat = u32::from_be_bytes([bytes[4], bytes[5], bytes[6], bytes[7]]);
        if (1..=8).contains(&nfat) && bytes.len() >= 12 {
            let cpu = u32::from_be_bytes([bytes[8], bytes[9], bytes[10], bytes[11]]);
            const CPU_X86: u32 = 7;
            const CPU_X86_64: u32 = 0x0100_0007;
            const CPU_ARM: u32 = 12;
            const CPU_ARM64: u32 = 0x0100_000C;
            return matches!(cpu, CPU_X86 | CPU_X86_64 | CPU_ARM | CPU_ARM64);
        }
    }
    false
}

fn bytes_contain(haystack: &[u8], needle: &[u8]) -> bool {
    haystack
        .windows(needle.len())
        .any(|window| window == needle)
}

/// Read the PE machine field. `0x014c` is x86 and `0x8664` is x86-64.
pub fn pe_machine(path: &Path) -> Option<u16> {
    use std::io::{Read, Seek, SeekFrom};
    let mut file = std::fs::File::open(path).ok()?;
    let mut head = [0u8; 0x40];
    if file.read(&mut head).ok()? < 0x40 || &head[0..2] != b"MZ" {
        return None;
    }
    let lfanew = u32::from_le_bytes([head[0x3c], head[0x3d], head[0x3e], head[0x3f]]) as u64;
    if lfanew > 1024 * 1024 {
        return None;
    }
    file.seek(SeekFrom::Start(lfanew)).ok()?;
    let mut pe = [0u8; 6];
    if file.read(&mut pe).ok()? < 6 || &pe[0..4] != b"PE\0\0" {
        return None;
    }
    Some(u16::from_le_bytes([pe[4], pe[5]]))
}
