// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

//! Slim guest runtimes. The bytes are never stored in Olympium.
//! A download comes from the per-system GitHub repository, or from a
//! configurable China mirror chosen from the client IP, and is checked
//! with SHA-256.

use crate::config::{Experience, Mirrors};
use crate::error::Error;
use crate::model::GuestSystem;
use crate::paths::Layout;
use flate2::read::GzDecoder;
use serde::Deserialize;
use sha2::{Digest, Sha256};
use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::thread;
use std::time::Duration;

const LOCK_JSON: &str = include_str!("../../../share/runtime-lock.json");

#[derive(Debug, Clone, Deserialize)]
pub struct RuntimeLock {
    pub schema: u32,
    pub product: String,
    pub license: String,
    pub runtimes: Vec<RuntimeEntry>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct RuntimeEntry {
    pub id: String,
    pub guest: String,
    pub edition: String,
    pub version: String,
    pub published: bool,
    pub repo: String,
    pub tag: String,
    pub summary: String,
    pub assets: AssetNames,
    pub sha256: std::collections::BTreeMap<String, String>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct AssetNames {
    pub x86_64: String,
    pub aarch64: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum MirrorSource {
    Github,
    China,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct MirrorChoice {
    pub source: MirrorSource,
    pub client_ip: Option<String>,
    pub country: Option<String>,
    pub reason: String,
}

#[derive(Debug, Clone, Copy)]
pub struct Progress {
    pub received: u64,
    pub total: Option<u64>,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct FetchReport {
    pub id: String,
    pub outcome: String,
    pub source: String,
    pub client_ip: Option<String>,
    pub country: Option<String>,
    pub reason: String,
    pub message: String,
    pub path: Option<String>,
}

pub trait Net {
    fn get_text(&self, url: &str) -> Result<String, Error>;
    fn probe(&self, url: &str) -> bool;
    fn download(
        &self,
        url: &str,
        dest: &Path,
        progress: &mut dyn FnMut(Progress),
    ) -> Result<(), Error>;
}

#[derive(Debug, Default, Clone)]
pub struct SystemNet;

impl Net for SystemNet {
    fn get_text(&self, url: &str) -> Result<String, Error> {
        let output = curl(["-fsSL", "--max-time", "8", url])?;
        if !output.status.success() {
            return Err(Error::Io {
                message: format!("Could not read {url}."),
            });
        }
        String::from_utf8(output.stdout).map_err(|_| Error::Io {
            message: format!("The reply from {url} was not text."),
        })
    }

    fn probe(&self, url: &str) -> bool {
        curl(["-fsI", "--max-time", "6", url])
            .map(|output| output.status.success())
            .unwrap_or(false)
    }

    fn download(
        &self,
        url: &str,
        dest: &Path,
        progress: &mut dyn FnMut(Progress),
    ) -> Result<(), Error> {
        if let Some(parent) = dest.parent() {
            fs::create_dir_all(parent).map_err(|err| Error::io(err, parent.display()))?;
        }
        let total = content_length(url);
        let mut child = Command::new("curl")
            .args(["-fL", "--retry", "3", "--retry-all-errors", "--retry-delay", "2", "-o"])
            .arg(dest)
            .arg(url)
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .map_err(|_| missing_curl())?;
        loop {
            let received = dest.metadata().map(|meta| meta.len()).unwrap_or(0);
            progress(Progress {
                received,
                total,
            });
            match child.try_wait() {
                Ok(Some(status)) => {
                    let received = dest.metadata().map(|meta| meta.len()).unwrap_or(0);
                    progress(Progress {
                        received,
                        total: total.or(Some(received)),
                    });
                    if status.success() {
                        return Ok(());
                    }
                    let _ = fs::remove_file(dest);
                    return Err(Error::Io {
                        message: format!("Download failed for {url}."),
                    });
                }
                Ok(None) => thread::sleep(Duration::from_millis(80)),
                Err(err) => return Err(Error::io(err, "curl")),
            }
        }
    }
}

pub fn bundled_lock() -> RuntimeLock {
    parse_lock(LOCK_JSON).expect("share/runtime-lock.json is valid")
}

pub fn parse_lock(text: &str) -> Result<RuntimeLock, Error> {
    serde_json::from_str(text).map_err(|err| Error::Config {
        message: format!("Could not read the runtime lock: {err}"),
    })
}

pub fn choose_mirror(mirrors: &Mirrors, net: &dyn Net) -> Result<MirrorChoice, Error> {
    match mirrors.prefer.trim() {
        "github" => Ok(MirrorChoice {
            source: MirrorSource::Github,
            client_ip: None,
            country: None,
            reason: "mirrors.prefer is github.".into(),
        }),
        "china" => Ok(MirrorChoice {
            source: MirrorSource::China,
            client_ip: None,
            country: None,
            reason: "mirrors.prefer is china.".into(),
        }),
        "auto" | "" => Ok(choose_auto(mirrors, net)),
        other => Err(Error::Config {
            message: format!("mirrors.prefer must be auto, github, or china, not {other}."),
        }),
    }
}

fn choose_auto(mirrors: &Mirrors, net: &dyn Net) -> MirrorChoice {
    match net.get_text(&mirrors.ip_lookup_url) {
        Ok(body) => match parse_ip_body(&body) {
            Some((ip, country)) if country.eq_ignore_ascii_case("CN") => MirrorChoice {
                source: MirrorSource::China,
                client_ip: Some(ip),
                country: Some(country),
                reason: "The client IP is in mainland China, so the China mirror is used."
                    .into(),
            },
            Some((ip, country)) => MirrorChoice {
                source: MirrorSource::Github,
                client_ip: Some(ip),
                country: Some(country.clone()),
                reason: format!(
                    "The client IP is in {country}, so GitHub is used. A China mirror is not selected."
                ),
            },
            None => probe_fallback(mirrors, net, None, None),
        },
        Err(_) => probe_fallback(mirrors, net, None, None),
    }
}

fn probe_fallback(
    mirrors: &Mirrors,
    net: &dyn Net,
    ip: Option<String>,
    country: Option<String>,
) -> MirrorChoice {
    if net.probe(&mirrors.probe_url) {
        MirrorChoice {
            source: MirrorSource::Github,
            client_ip: ip,
            country,
            reason: "The IP lookup did not return a country. GitHub answered, so GitHub is used."
                .into(),
        }
    } else {
        MirrorChoice {
            source: MirrorSource::China,
            client_ip: ip,
            country,
            reason: "The IP lookup did not return a country and GitHub did not answer, so the China mirror is used.".into(),
        }
    }
}

pub fn parse_ip_body(body: &str) -> Option<(String, String)> {
    let value: serde_json::Value = serde_json::from_str(body).ok()?;
    let ip = value
        .get("ip")
        .or_else(|| value.get("query"))
        .and_then(|item| item.as_str())
        .map(str::trim)
        .filter(|ip| is_ip(ip))?;
    let country = value
        .get("country")
        .or_else(|| value.get("countryCode"))
        .or_else(|| value.get("country_code"))
        .and_then(|item| item.as_str())
        .map(str::trim)
        .filter(|code| code.len() == 2 && code.chars().all(|c| c.is_ascii_alphabetic()))?;
    Some((ip.to_string(), country.to_ascii_uppercase()))
}

pub fn candidate_urls(canonical: &str, choice: &MirrorChoice, mirrors: &Mirrors) -> Vec<String> {
    let prefixed = match choice.source {
        MirrorSource::China => prefix_url(&mirrors.china_prefix, canonical),
        MirrorSource::Github => prefix_url(&mirrors.github_prefix, canonical),
    };
    let mut urls = Vec::new();
    if let Some(url) = prefixed {
        urls.push(url);
    }
    if !urls.iter().any(|url| url == canonical) {
        urls.push(canonical.to_string());
    }
    urls
}

pub fn fetch_runtime(
    id: &str,
    lock: &RuntimeLock,
    mirrors: &Mirrors,
    net: &dyn Net,
    layout: &Layout,
    arch: &str,
    mut progress: impl FnMut(Progress),
) -> Result<FetchReport, Error> {
    let entry = lock
        .runtimes
        .iter()
        .find(|entry| entry.id == id)
        .ok_or_else(|| Error::Target {
            message: format!("There is no slim runtime named {id}."),
        })?;
    let choice = choose_mirror(mirrors, net)?;
    if !entry.published {
        return Ok(report(
            entry,
            &choice,
            "not_published",
            None,
            format!(
                "Chosen source: {} ({}). The slim {} image is not published yet at {}. Olympium does not bundle it.",
                source_label(choice.source),
                choice.reason,
                entry.id,
                entry.repo
            ),
        ));
    }
    let asset = asset_name(entry, arch)?;
    let sha = entry
        .sha256
        .get(arch)
        .filter(|value| value.len() == 64 && value.chars().all(|c| c.is_ascii_hexdigit()))
        .cloned()
        .ok_or_else(|| Error::Config {
            message: format!("The slim {} image is not published for {arch}.", entry.id),
        })?;
    let dest_dir = layout.runtimes.join(&entry.id);
    let marker = dest_dir.join("EDITION");
    if marker.is_file() {
        let installed = fs::read_to_string(&marker).unwrap_or_default();
        if installed.contains(&entry.version) {
            return Ok(report(
                entry,
                &choice,
                "ready",
                Some(dest_dir.display().to_string()),
                format!(
                    "The slim {} image {} is already installed.",
                    entry.id, entry.version
                ),
            ));
        }
    }
    let canonical = format!(
        "{}/releases/download/{}/{}",
        entry.repo.trim_end_matches('/'),
        entry.tag,
        asset
    );
    let archive = layout.cache.join(format!("{id}-{arch}.tar.gz.partial"));
    let mut last_error = None;
    for url in candidate_urls(&canonical, &choice, mirrors) {
        progress(Progress {
            received: 0,
            total: None,
        });
        match net.download(&url, &archive, &mut progress) {
            Ok(()) => match verify_sha(&archive, &sha) {
                Ok(()) => {
                    unpack_tar_gz(&archive, &dest_dir)?;
                    let _ = fs::remove_file(&archive);
                    return Ok(report(
                        entry,
                        &choice,
                        "downloaded",
                        Some(dest_dir.display().to_string()),
                        format!(
                            "Downloaded the slim {} image from {url} and checked SHA-256.",
                            entry.id
                        ),
                    ));
                }
                Err(err) => {
                    let _ = fs::remove_file(&archive);
                    last_error = Some(err);
                }
            },
            Err(err) => last_error = Some(err),
        }
    }
    Err(last_error.unwrap_or_else(|| Error::Io {
        message: format!("Could not download the slim {} image.", entry.id),
    }))
}

pub fn runtime_rows(lock: &RuntimeLock, layout: &Layout) -> Vec<String> {
    lock.runtimes
        .iter()
        .map(|entry| {
            let installed = layout.runtimes.join(&entry.id).join("EDITION").is_file();
            let state = if installed {
                "installed"
            } else if entry.published {
                "not_installed"
            } else {
                "not_published"
            };
            format!(
                "{}\t{}\t{}\t{}",
                entry.id, state, entry.edition, entry.summary
            )
        })
        .collect()
}

pub fn memory_budget_mib(experience: &Experience, available_mib: Option<u64>) -> u64 {
    let min = experience.memory_min_mib.max(256);
    let max = experience.memory_max_mib.max(min);
    if experience.memory_mode == "manual" {
        return experience.memory_manual_mib.clamp(min, max);
    }
    let Some(available) = available_mib else {
        return min;
    };
    const HEADROOM: u64 = 2048;
    let usable = available.saturating_sub(HEADROOM);
    let quarter = usable / 4;
    quarter.clamp(min, max)
}

pub fn available_memory_mib() -> Option<u64> {
    let text = fs::read_to_string("/proc/meminfo").ok()?;
    for line in text.lines() {
        if let Some(rest) = line.strip_prefix("MemAvailable:") {
            let kb: u64 = rest.split_whitespace().next()?.parse().ok()?;
            return Some(kb / 1024);
        }
    }
    None
}

pub fn guest_runtime_id(guest: GuestSystem) -> Option<&'static str> {
    match guest {
        GuestSystem::Android => Some("android"),
        GuestSystem::Linux => Some("linux"),
        GuestSystem::OpenHarmony => Some("openharmony"),
        GuestSystem::Windows => Some("wine"),
        GuestSystem::MacOs | GuestSystem::Ios => None,
    }
}

pub fn host_arch() -> &'static str {
    match std::env::consts::ARCH {
        "x86_64" => "x86_64",
        "aarch64" => "aarch64",
        other => other,
    }
}

/// The slim Wine loader, when this archive says it is a Linux binary.
/// Olympium does not treat a Linux ELF as a macOS or Windows program.
/// Command that boots an extracted slim Linux guest.
/// `smoke` runs the probe built into the image. `start` boots its init.
pub struct GuestCommand {
    pub program: PathBuf,
    pub args: Vec<String>,
    pub arch: String,
}

pub fn linux_guest_command(layout: &Layout, action: &str) -> Result<GuestCommand, Error> {
    if action != "smoke" && action != "start" {
        return Err(Error::Target {
            message: format!("Guest action must be smoke or start, not {action}."),
        });
    }
    let root = layout.runtimes.join("linux");
    if !root.join("EDITION").is_file() {
        return Err(Error::NotConfigured {
            engine: "linux",
            message: "The slim Linux image is not installed. `olympium fetch --guest linux` downloads it and checks SHA-256.".into(),
        });
    }
    for rel in ["ARCH", "boot/vmlinuz", "boot/initramfs", "rootfs.img", "bin/linkspan-runtime"] {
        if !root.join(rel).is_file() {
            return Err(Error::Io {
                message: format!("The extracted Linux image is missing {rel}."),
            });
        }
    }
    let arch = fs::read_to_string(root.join("ARCH"))
        .map_err(|err| Error::io(err, root.join("ARCH").display()))?;
    let arch = arch.trim().to_string();
    if arch != "x86_64" && arch != "aarch64" {
        return Err(Error::Target {
            message: format!("The extracted Linux image says arch {arch}, which this client does not boot."),
        });
    }
    let program = root.join("bin/linkspan-runtime");
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut perms = fs::metadata(&program)
            .map_err(|err| Error::io(err, program.display()))?
            .permissions();
        perms.set_mode(perms.mode() | 0o755);
        fs::set_permissions(&program, perms).map_err(|err| Error::io(err, program.display()))?;
    }
    Ok(GuestCommand {
        program,
        args: vec![action.to_string()],
        arch,
    })
}

pub fn installed_wine(layout: &Layout) -> Option<std::path::PathBuf> {
    let root = layout.runtimes.join("wine");
    let host = fs::read_to_string(root.join("HOST_OS")).unwrap_or_default();
    if host.trim() != "linux" {
        return None;
    }
    let bin = root.join("bin/wine");
    if bin.is_file() {
        Some(bin)
    } else {
        None
    }
}

fn report(
    entry: &RuntimeEntry,
    choice: &MirrorChoice,
    outcome: &str,
    path: Option<String>,
    message: String,
) -> FetchReport {
    FetchReport {
        id: entry.id.clone(),
        outcome: outcome.to_string(),
        source: source_label(choice.source).to_string(),
        client_ip: choice.client_ip.clone(),
        country: choice.country.clone(),
        reason: choice.reason.clone(),
        message,
        path,
    }
}

fn source_label(source: MirrorSource) -> &'static str {
    match source {
        MirrorSource::Github => "github",
        MirrorSource::China => "china",
    }
}

fn asset_name(entry: &RuntimeEntry, arch: &str) -> Result<String, Error> {
    let name = match arch {
        "x86_64" => Some(entry.assets.x86_64.as_str()),
        "aarch64" => Some(entry.assets.aarch64.as_str()),
        _ => None,
    };
    name.filter(|value| !value.is_empty())
        .map(str::to_string)
        .ok_or_else(|| Error::Target {
            message: format!("The slim {} image has no build for {arch}.", entry.id),
        })
}

fn prefix_url(prefix: &str, canonical: &str) -> Option<String> {
    let prefix = prefix.trim();
    if prefix.is_empty() {
        return None;
    }
    Some(format!("{prefix}{canonical}"))
}

fn is_ip(value: &str) -> bool {
    if value.len() > 64 || value.is_empty() {
        return false;
    }
    value
        .chars()
        .all(|c| c.is_ascii_hexdigit() || matches!(c, '.' | ':'))
        && (value.contains('.') || value.contains(':'))
}

fn verify_sha(path: &Path, expected: &str) -> Result<(), Error> {
    let mut file = fs::File::open(path).map_err(|err| Error::io(err, path.display()))?;
    let mut hasher = Sha256::new();
    let mut buf = [0u8; 64 * 1024];
    loop {
        let read = file
            .read(&mut buf)
            .map_err(|err| Error::io(err, path.display()))?;
        if read == 0 {
            break;
        }
        hasher.update(&buf[..read]);
    }
    let digest = hex_encode(&hasher.finalize());
    if digest.eq_ignore_ascii_case(expected) {
        Ok(())
    } else {
        Err(Error::Io {
            message: "SHA-256 did not match the runtime lock. The file was discarded.".into(),
        })
    }
}

fn hex_encode(bytes: &[u8]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut out = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        out.push(HEX[(byte >> 4) as usize] as char);
        out.push(HEX[(byte & 0xf) as usize] as char);
    }
    out
}

fn unpack_tar_gz(archive: &Path, dest: &Path) -> Result<(), Error> {
    if dest.exists() {
        fs::remove_dir_all(dest).map_err(|err| Error::io(err, dest.display()))?;
    }
    fs::create_dir_all(dest).map_err(|err| Error::io(err, dest.display()))?;
    let file = fs::File::open(archive).map_err(|err| Error::io(err, archive.display()))?;
    let decoder = GzDecoder::new(file);
    let mut archive = tar::Archive::new(decoder);
    for entry in archive.entries().map_err(|err| Error::Io {
        message: format!("Could not read the slim image: {err}"),
    })? {
        let mut entry = entry.map_err(|err| Error::Io {
            message: format!("Could not read the slim image: {err}"),
        })?;
        entry.unpack_in(dest).map_err(|err| Error::Io {
            message: format!("Refusing to unpack the slim image outside its directory: {err}"),
        })?;
    }
    Ok(())
}

fn curl(
    args: impl IntoIterator<Item = impl AsRef<std::ffi::OsStr>>,
) -> Result<std::process::Output, Error> {
    Command::new("curl")
        .args(args)
        .stdin(Stdio::null())
        .output()
        .map_err(|_| missing_curl())
}

/// The last Content-Length in a curl `-I` transcript. Redirects emit more
/// than one, and the final response is the file the client will save.
pub fn last_content_length(headers: &str) -> Option<u64> {
    let mut found = None;
    for line in headers.lines() {
        let Some((name, value)) = line.split_once(':') else {
            continue;
        };
        if name.eq_ignore_ascii_case("content-length") {
            if let Ok(size) = value.trim().parse::<u64>() {
                if size > 0 {
                    found = Some(size);
                }
            }
        }
    }
    found
}

fn content_length(url: &str) -> Option<u64> {
    let output = curl(["-fsIL", "--max-time", "20", url]).ok()?;
    if !output.status.success() {
        return None;
    }
    let headers = String::from_utf8(output.stdout).ok()?;
    last_content_length(&headers)
}

fn missing_curl() -> Error {
    Error::EngineMissing {
        engine: "runtime",
        message: "curl is required to choose a download source and to fetch a slim image. Olympium does not bundle the image.".into(),
    }
}

pub fn write_mirror(layout: &Layout, choice: &MirrorChoice) -> Result<(), Error> {
    if let Some(parent) = layout.mirror.parent() {
        fs::create_dir_all(parent).map_err(|err| Error::io(err, parent.display()))?;
    }
    let text = serde_json::to_string_pretty(choice).map_err(|err| Error::Io {
        message: format!("Could not record the mirror choice: {err}"),
    })?;
    fs::write(&layout.mirror, text).map_err(|err| Error::io(err, layout.mirror.display()))
}

pub fn read_mirror(layout: &Layout) -> Option<MirrorChoice> {
    let text = fs::read_to_string(&layout.mirror).ok()?;
    serde_json::from_str(&text).ok()
}

#[cfg(test)]
mod ip_parse {
    use super::{installed_wine, last_content_length, linux_guest_command, parse_ip_body};
    use crate::paths::Layout;
    use std::fs;

    #[test]
    fn linux_guest_command_requires_the_extracted_image() {
        let root = std::env::temp_dir().join(format!("olympium-guest-cmd-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        let layout = Layout::under(&root);
        assert!(linux_guest_command(&layout, "smoke").is_err());
        let dir = layout.runtimes.join("linux");
        fs::create_dir_all(dir.join("boot")).unwrap();
        fs::create_dir_all(dir.join("bin")).unwrap();
        fs::write(dir.join("EDITION"), b"linux-slim 0.1.0\n").unwrap();
        fs::write(dir.join("ARCH"), b"x86_64\n").unwrap();
        fs::write(dir.join("boot/vmlinuz"), b"kernel").unwrap();
        fs::write(dir.join("boot/initramfs"), b"initrd").unwrap();
        fs::write(dir.join("rootfs.img"), b"disk").unwrap();
        fs::write(dir.join("bin/linkspan-runtime"), b"#!/bin/sh\n").unwrap();
        let command = linux_guest_command(&layout, "smoke").unwrap();
        assert_eq!(command.arch, "x86_64");
        assert_eq!(command.args, vec!["smoke".to_string()]);
        assert!(command.program.ends_with("bin/linkspan-runtime"));
        assert!(linux_guest_command(&layout, "window").is_err());
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn installed_wine_requires_the_linux_marker() {
        let root =
            std::env::temp_dir().join(format!("olympium-wine-marker-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        let layout = Layout::under(&root);
        assert!(installed_wine(&layout).is_none());
        let dir = layout.runtimes.join("wine/bin");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("wine"), b"wine").unwrap();
        fs::write(layout.runtimes.join("wine/HOST_OS"), b"linux\n").unwrap();
        assert!(installed_wine(&layout).unwrap().ends_with("bin/wine"));
        fs::write(layout.runtimes.join("wine/HOST_OS"), b"macos\n").unwrap();
        assert!(installed_wine(&layout).is_none());
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn content_length_uses_the_final_response() {
        let headers = "HTTP/1.1 302\r\nContent-Length: 0\r\n\r\nHTTP/1.1 200\r\nContent-Length: 21571731\r\n";
        assert_eq!(last_content_length(headers), Some(21571731));
        assert_eq!(last_content_length("HTTP/1.1 200\r\n"), None);
    }

    #[test]
    fn reads_ipinfo_and_ip_api_shapes() {
        let (ip, country) = parse_ip_body(r#"{"ip":"1.2.3.4","country":"CN"}"#).unwrap();
        assert_eq!(ip, "1.2.3.4");
        assert_eq!(country, "CN");
        let (ip, country) = parse_ip_body(r#"{"query":"8.8.8.8","countryCode":"us"}"#).unwrap();
        assert_eq!(ip, "8.8.8.8");
        assert_eq!(country, "US");
    }
}
