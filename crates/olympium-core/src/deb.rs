// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use crate::error::Error;
use crate::probe::which;
use crate::util::{lossy, normalize_rel, safe_join};
use flate2::read::GzDecoder;
use flate2::write::GzEncoder;
use flate2::Compression;
use std::fs::{self, File};
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::process::Command;
use tar::{Archive, Builder, Header};

const MAX_IN_MEMORY: u64 = 64 * 1024 * 1024;

pub struct DebMember<'a> {
    pub name: &'a str,
    pub bytes: &'a [u8],
}

/// Build a gzip Debian package. Used for the sample guest and tests.
#[allow(dead_code)]
pub fn write_gzip_deb(path: &Path, members: &[DebMember<'_>]) -> Result<(), Error> {
    let bytes = encode_ar(members)?;
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|err| Error::io(err, parent.display()))?;
    }
    fs::write(path, bytes).map_err(|err| Error::io(err, path.display()))?;
    Ok(())
}

pub fn sample_deb_bytes(script_body: &str) -> Result<Vec<u8>, Error> {
    let control = b"Package: hello-olympium\nVersion: 1.0\nArchitecture: all\nMaintainer: Ye Zicheng <noreply@example.invalid>\nDescription: Sample Linux guest for Olympium\n";
    let control_tar = tar_gz(&[("control", control, 0o644)])?;
    let data_tar = tar_gz(&[("usr/bin/hello-olympium", script_body.as_bytes(), 0o755)])?;
    encode_ar(&[
        DebMember {
            name: "debian-binary",
            bytes: b"2.0\n",
        },
        DebMember {
            name: "control.tar.gz",
            bytes: &control_tar,
        },
        DebMember {
            name: "data.tar.gz",
            bytes: &data_tar,
        },
    ])
}

pub fn extract_deb(archive: &Path, dest: &Path) -> Result<Option<String>, Error> {
    assert_extract_dest(dest)?;
    if dest.exists() {
        fs::remove_dir_all(dest).map_err(|err| Error::io(err, dest.display()))?;
    }
    fs::create_dir_all(dest).map_err(|err| Error::io(err, dest.display()))?;
    let meta = fs::metadata(archive).map_err(|err| Error::io(err, archive.display()))?;
    if meta.len() > MAX_IN_MEMORY {
        return extract_with_dpkg(archive, dest);
    }
    let bytes = fs::read(archive).map_err(|err| Error::io(err, archive.display()))?;
    match deb_payload_kind(&bytes) {
        DebPayload::Gzip => extract_gzip_deb_bytes(&bytes, dest),
        DebPayload::External => extract_with_dpkg(archive, dest),
        DebPayload::Invalid(err) => Err(err),
    }
}

enum DebPayload {
    Gzip,
    External,
    Invalid(Error),
}

fn deb_payload_kind(bytes: &[u8]) -> DebPayload {
    let members = match parse_ar(bytes) {
        Ok(members) => members,
        Err(err) => return DebPayload::Invalid(err),
    };
    if members.iter().any(|(name, _)| name == "data.tar.gz") {
        DebPayload::Gzip
    } else if members.iter().any(|(name, _)| name.starts_with("data.tar")) {
        DebPayload::External
    } else {
        DebPayload::Invalid(Error::Target {
            message: "This archive has no Debian data member.".into(),
        })
    }
}

pub fn extract_gzip_deb_bytes(bytes: &[u8], dest: &Path) -> Result<Option<String>, Error> {
    let members = parse_ar(bytes)?;
    let control = members
        .iter()
        .find(|(name, _)| name == "control.tar.gz")
        .map(|(_, bytes)| bytes.as_slice());
    let data = members
        .iter()
        .find(|(name, _)| name == "data.tar.gz")
        .map(|(_, bytes)| bytes.as_slice());
    let Some(data) = data else {
        let names: Vec<_> = members.iter().map(|(name, _)| name.as_str()).collect();
        return Err(Error::Target {
            message: format!(
                "This .deb has no gzip data archive (found {}). xz and zstd packages need dpkg-deb.",
                names.join(", ")
            ),
        });
    };
    unpack_tar_gz(data, dest)?;
    let package = control.and_then(package_name_from_control_tar);
    Ok(package)
}

fn extract_with_dpkg(archive: &Path, dest: &Path) -> Result<Option<String>, Error> {
    let tool = which("dpkg-deb").ok_or_else(|| Error::EngineMissing {
        engine: "deb",
        message: "This .deb is not a gzip archive Olympium can read itself, and dpkg-deb is not installed. Install the dpkg package, or use a gzip .deb.".into(),
    })?;
    let status = Command::new(&tool)
        .arg("-x")
        .arg(archive)
        .arg(dest)
        .status()
        .map_err(|err| Error::io(err, "dpkg-deb"))?;
    if !status.success() {
        return Err(Error::Failed {
            message: "dpkg-deb could not extract this package.".into(),
            guest_exit_code: status.code(),
            stdout: String::new(),
            stderr: String::new(),
        });
    }
    let output = Command::new(&tool)
        .args(["-f", archive.to_string_lossy().as_ref(), "Package"])
        .output();
    let package = output.ok().and_then(|out| {
        if out.status.success() {
            let name = lossy(&out.stdout).trim().to_string();
            if name.is_empty() {
                None
            } else {
                Some(name)
            }
        } else {
            None
        }
    });
    Ok(package)
}

pub fn find_payload(
    dest: &Path,
    selector: Option<&str>,
    package_name: Option<&str>,
) -> Result<PathBuf, Error> {
    if let Some(selector) = selector {
        if selector.starts_with('-') || selector.contains('\0') {
            return Err(Error::Target {
                message: "The program name inside the package is not valid.".into(),
            });
        }
        if Path::new(selector).is_absolute() || selector.contains("..") {
            return Err(Error::Target {
                message: "The program name has to stay inside the package.".into(),
            });
        }
        if selector.contains('/') || selector.contains('\\') {
            let rel = Path::new(selector);
            let Some(path) = safe_join(dest, rel) else {
                return Err(Error::Target {
                    message: "The program name has to stay inside the package.".into(),
                });
            };
            if is_regular(&path) && contained(dest, &path) {
                return Ok(path);
            }
            return Err(Error::Target {
                message: format!("The package does not contain {selector}."),
            });
        }
        if let Some(found) = find_named(dest, selector) {
            return Ok(found);
        }
        return Err(Error::Target {
            message: format!("The package does not contain a program named {selector}."),
        });
    }
    let bins = bin_files(dest);
    if let Some(package_name) = package_name {
        if let Some(found) = bins.iter().find(|path| file_name(path) == package_name) {
            return Ok(found.clone());
        }
    }
    if bins.len() == 1 {
        return Ok(bins[0].clone());
    }
    if bins.is_empty() {
        return Err(Error::Target {
            message: "The package does not contain a program under usr/bin, usr/local/bin, bin, or usr/sbin.".into(),
        });
    }
    let names: Vec<_> = bins.iter().take(12).map(|path| file_name(path)).collect();
    Err(Error::Target {
        message: format!(
            "This package contains several programs: {}. Run it again with --bin and one of those names.",
            names.join(", ")
        ),
    })
}

fn find_named(dest: &Path, name: &str) -> Option<PathBuf> {
    bin_files(dest)
        .into_iter()
        .find(|path| file_name(path) == name)
        .or_else(|| {
            walk_files(dest)
                .into_iter()
                .find(|path| file_name(path) == name)
        })
}

fn bin_files(dest: &Path) -> Vec<PathBuf> {
    let mut found = Vec::new();
    for rel in ["usr/bin", "usr/local/bin", "usr/games", "bin", "usr/sbin"] {
        let dir = dest.join(rel);
        let Ok(entries) = fs::read_dir(&dir) else {
            continue;
        };
        let mut names: Vec<_> = entries.filter_map(|entry| entry.ok()).collect();
        names.sort_by_key(|entry| entry.file_name());
        for entry in names {
            let path = entry.path();
            if is_regular(&path) {
                found.push(path);
            }
        }
    }
    found
}

fn walk_files(dest: &Path) -> Vec<PathBuf> {
    let mut out = Vec::new();
    let mut stack = vec![dest.to_path_buf()];
    while let Some(dir) = stack.pop() {
        let Ok(entries) = fs::read_dir(&dir) else {
            continue;
        };
        for entry in entries.filter_map(|entry| entry.ok()) {
            let path = entry.path();
            if path.is_dir() {
                stack.push(path);
            } else if is_regular(&path) {
                out.push(path);
            }
        }
    }
    out
}

fn file_name(path: &Path) -> String {
    path.file_name()
        .map(|name| name.to_string_lossy().into_owned())
        .unwrap_or_default()
}

fn is_regular(path: &Path) -> bool {
    fs::symlink_metadata(path)
        .map(|meta| meta.file_type().is_file())
        .unwrap_or(false)
}

fn contained(dest: &Path, file: &Path) -> bool {
    let Ok(dest) = dest.canonicalize() else {
        return false;
    };
    let Ok(file) = file.canonicalize() else {
        return false;
    };
    file.starts_with(dest)
}

fn assert_extract_dest(dest: &Path) -> Result<(), Error> {
    if dest.as_os_str().is_empty() || dest == Path::new("/") || dest.components().count() < 2 {
        return Err(Error::Target {
            message: "Refusing to extract into an unsafe directory.".into(),
        });
    }
    Ok(())
}

pub fn encode_ar(members: &[DebMember<'_>]) -> Result<Vec<u8>, Error> {
    let mut out = b"!<arch>\n".to_vec();
    for member in members {
        let header = ar_header(member.name, member.bytes.len())?;
        out.extend_from_slice(&header);
        out.extend_from_slice(member.bytes);
        if member.bytes.len() % 2 == 1 {
            out.push(b'\n');
        }
    }
    Ok(out)
}

fn ar_header(name: &str, size: usize) -> Result<[u8; 60], Error> {
    let name_field = format!("{name}/");
    if name_field.len() > 16 {
        return Err(Error::Target {
            message: format!("archive member name is too long: {name}"),
        });
    }
    let text = format!(
        "{:<16}{:<12}{:<6}{:<6}{:<8}{:<10}`\n",
        name_field, "0", "0", "0", "100644", size
    );
    if text.len() != 60 {
        return Err(Error::Target {
            message: "internal archive header error".into(),
        });
    }
    let mut header = [0u8; 60];
    header.copy_from_slice(text.as_bytes());
    Ok(header)
}

pub fn parse_ar(data: &[u8]) -> Result<Vec<(String, Vec<u8>)>, Error> {
    if !data.starts_with(b"!<arch>\n") {
        return Err(Error::Target {
            message:
                "This file is not an ar archive, so it is not a Debian package Olympium can read."
                    .into(),
        });
    }
    let mut index = 8;
    let mut members = Vec::new();
    while index + 60 <= data.len() {
        let header = &data[index..index + 60];
        index += 60;
        if &header[58..60] != b"`\n" {
            return Err(Error::Target {
                message: "This Debian package has a broken archive header.".into(),
            });
        }
        let name_raw = lossy(&header[0..16]);
        let size_text = lossy(&header[48..58]);
        let size: usize = size_text.trim().parse().map_err(|_| Error::Target {
            message: "This Debian package has a broken member size.".into(),
        })?;
        if index + size > data.len() {
            return Err(Error::Target {
                message: "This Debian package is truncated.".into(),
            });
        }
        let body = data[index..index + size].to_vec();
        index += size;
        if size % 2 == 1 {
            index += 1;
        }
        let name = name_raw.trim().trim_end_matches('/').trim().to_string();
        if name.is_empty() || name == "/" || name == "//" || name.starts_with('/') {
            continue;
        }
        members.push((name, body));
    }
    Ok(members)
}

fn tar_gz(files: &[(&str, &[u8], u32)]) -> Result<Vec<u8>, Error> {
    let encoder = GzEncoder::new(Vec::new(), Compression::default());
    let mut builder = Builder::new(encoder);
    for (path, bytes, mode) in files {
        let mut header = Header::new_gnu();
        header
            .set_path(path)
            .map_err(|err| Error::io(err, "tar header"))?;
        header.set_size(bytes.len() as u64);
        header.set_mode(*mode);
        header.set_cksum();
        builder
            .append(&header, *bytes)
            .map_err(|err| Error::io(err, "tar append"))?;
    }
    let encoder = builder
        .into_inner()
        .map_err(|err| Error::io(err, "tar finish"))?;
    encoder
        .finish()
        .map_err(|err| Error::io(err, "gzip finish"))
}

fn unpack_tar_gz(bytes: &[u8], dest: &Path) -> Result<(), Error> {
    if bytes.len() < 2 || bytes[0] != 0x1f || bytes[1] != 0x8b {
        return Err(Error::Target {
            message: "The package data is not gzip compressed.".into(),
        });
    }
    let decoder = GzDecoder::new(bytes);
    unpack_tar(decoder, dest)
}

fn unpack_tar<R: Read>(reader: R, dest: &Path) -> Result<(), Error> {
    let mut archive = Archive::new(reader);
    let entries = archive.entries().map_err(|err| Error::io(err, "tar"))?;
    for entry in entries {
        let mut entry = entry.map_err(|err| Error::io(err, "tar entry"))?;
        let raw = entry
            .path()
            .map_err(|err| Error::io(err, "tar path"))?
            .into_owned();
        let rel = normalize_rel(&raw);
        if rel.as_os_str().is_empty() {
            continue;
        }
        let Some(out) = safe_join(dest, &rel) else {
            return Err(Error::Target {
                message: format!("The package contains an unsafe path ({}).", raw.display()),
            });
        };
        let kind = entry.header().entry_type();
        if kind.is_dir() {
            fs::create_dir_all(&out).map_err(|err| Error::io(err, out.display()))?;
            continue;
        }
        if !kind.is_file() {
            continue;
        }
        if let Some(parent) = out.parent() {
            fs::create_dir_all(parent).map_err(|err| Error::io(err, parent.display()))?;
        }
        let mut file = File::create(&out).map_err(|err| Error::io(err, out.display()))?;
        io::copy(&mut entry, &mut file).map_err(|err| Error::io(err, out.display()))?;
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let mode = entry.header().mode().unwrap_or(0o644) & 0o777;
            fs::set_permissions(&out, fs::Permissions::from_mode(mode))
                .map_err(|err| Error::io(err, out.display()))?;
        }
    }
    Ok(())
}

fn package_name_from_control_tar(bytes: &[u8]) -> Option<String> {
    let decoder = GzDecoder::new(bytes);
    let mut archive = Archive::new(decoder);
    let entries = archive.entries().ok()?;
    for entry in entries {
        let mut entry = entry.ok()?;
        let path = entry.path().ok()?.into_owned();
        let name = path.file_name()?.to_string_lossy().to_string();
        if name != "control" {
            continue;
        }
        let mut text = String::new();
        entry.read_to_string(&mut text).ok()?;
        return control_package_name(&text);
    }
    None
}

fn control_package_name(text: &str) -> Option<String> {
    for line in text.lines() {
        if let Some(rest) = line.strip_prefix("Package:") {
            let name = rest.trim();
            if !name.is_empty() {
                return Some(name.to_string());
            }
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn scratch() -> PathBuf {
        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let path = std::env::temp_dir().join(format!("olympium-deb-{nanos}"));
        fs::create_dir_all(&path).unwrap();
        path
    }

    #[test]
    fn gzip_deb_roundtrip() {
        let root = scratch();
        let deb = root.join("hello.deb");
        let bytes = sample_deb_bytes("#!/bin/sh\nprintf 'ok\\n'\n").unwrap();
        fs::write(&deb, bytes).unwrap();
        let dest = root.join("out");
        let name = extract_deb(&deb, &dest).unwrap();
        assert_eq!(name.as_deref(), Some("hello-olympium"));
        let program = find_payload(&dest, None, name.as_deref()).unwrap();
        assert!(program.ends_with("hello-olympium"));
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn rejects_parent_paths() {
        let root = scratch();
        let dest = root.join("out");
        fs::create_dir_all(&dest).unwrap();
        let control = tar_gz(&[("control", b"Package: evil\n", 0o644)]).unwrap();
        let data = gzip_bytes(&raw_tar("../../tmp/olympium-evil", b"nope\n"));
        let bytes = encode_ar(&[
            DebMember {
                name: "debian-binary",
                bytes: b"2.0\n",
            },
            DebMember {
                name: "control.tar.gz",
                bytes: &control,
            },
            DebMember {
                name: "data.tar.gz",
                bytes: &data,
            },
        ])
        .unwrap();
        let err = extract_gzip_deb_bytes(&bytes, &dest).unwrap_err();
        assert!(err.to_string().contains("unsafe"), "{err}");
        let _ = fs::remove_dir_all(root);
    }

    fn gzip_bytes(bytes: &[u8]) -> Vec<u8> {
        use std::io::Write;
        let mut encoder = GzEncoder::new(Vec::new(), Compression::default());
        encoder.write_all(bytes).unwrap();
        encoder.finish().unwrap()
    }

    fn raw_tar(name: &str, data: &[u8]) -> Vec<u8> {
        let mut header = vec![0u8; 512];
        let name_bytes = name.as_bytes();
        header[..name_bytes.len()].copy_from_slice(name_bytes);
        let size = format!("{:011o}", data.len());
        header[124..135].copy_from_slice(size.as_bytes());
        header[156] = b'0';
        header[257..262].copy_from_slice(b"ustar");
        header[263] = b'0';
        header[264] = b'0';
        for byte in &mut header[148..156] {
            *byte = b' ';
        }
        let sum: u32 = header.iter().map(|byte| u32::from(*byte)).sum();
        let checksum = format!("{sum:06o}\0 ");
        header[148..156].copy_from_slice(checksum.as_bytes());
        let mut out = header;
        out.extend_from_slice(data);
        let pad = (512 - (data.len() % 512)) % 512;
        out.extend(std::iter::repeat(0).take(pad));
        out.extend(std::iter::repeat(0).take(1024));
        out
    }
}
