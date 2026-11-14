// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

//! OpenHarmony packages and the runtime Olympium is willing to talk to.
//!
//! A HAP or app bundle is installed only through OpenHarmony's own tools
//! (`bm` / `aa` on device, or `hdc` against a runtime that identifies itself
//! as OpenHarmony). Nothing here converts a HAP into an APK, calls Wine, or
//! accepts a commercial HarmonyOS image.

use crate::config::Config;
use crate::error::Error;
use crate::model::PackageKind;
use crate::probe::Probe;
use crate::zipdoc::{self, ZipEntry};
use std::io::Read;
use std::path::Path;
use std::process::{Command, Stdio};
use std::thread;
use std::time::{Duration, Instant};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OhIdentity {
    OpenHarmony,
    CommercialHarmony,
    Other,
    Unknown,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SignatureState {
    /// A signature block is present. Olympium does not decide that it is trusted.
    Present,
    /// No signature block was found.
    Absent,
    /// The package could not be read far enough to tell.
    Unknown,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PackageInfo {
    pub kind: PackageKind,
    pub signature: SignatureState,
    pub bundle_name: Option<String>,
    pub ability: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OhFacts {
    pub identity: OhIdentity,
    pub developer_mode: Option<bool>,
    pub device_connected: bool,
    pub hdc: Option<String>,
    pub runtime: Option<String>,
    pub bridge: Option<String>,
    pub image: Option<String>,
}

pub trait OhSource {
    fn facts(
        &self,
        host: crate::model::HostPlatform,
        config: &Config,
        probe: &dyn Probe,
    ) -> OhFacts;
}

#[derive(Debug, Default, Clone)]
pub struct SystemOpenHarmony;

impl OhSource for SystemOpenHarmony {
    fn facts(
        &self,
        host: crate::model::HostPlatform,
        config: &Config,
        probe: &dyn Probe,
    ) -> OhFacts {
        let image = nonempty(&config.engines.openharmony.image);
        let edition = image.as_ref().and_then(|dir| read_edition(Path::new(dir)));
        let mut identity = edition
            .as_deref()
            .map(identity_from_text)
            .unwrap_or(OhIdentity::Unknown);
        let hdc = first_file(
            [
                nonempty(&config.engines.openharmony.hdc),
                image
                    .as_ref()
                    .map(|dir| Path::new(dir).join("bin/hdc"))
                    .filter(|path| path.is_file())
                    .map(|path| path.to_string_lossy().into_owned()),
                probe.lookup("hdc"),
            ]
            .into_iter()
            .flatten(),
        );
        let runtime = image
            .as_ref()
            .and_then(|dir| existing_file(&Path::new(dir).join("bin/linkspan-runtime")));
        let bridge = first_file(
            [
                nonempty(&config.engines.openharmony.bridge),
                image
                    .as_ref()
                    .and_then(|dir| existing_file(&Path::new(dir).join("bin/linkspan-surface"))),
            ]
            .into_iter()
            .flatten(),
        );
        let mut developer = None;
        let mut device_connected = false;
        if let Some(bin) = hdc.as_deref() {
            if let Some(name) = hdc_param(bin, "const.product.os.dist.name") {
                identity = identity_from_text(&name);
                device_connected = true;
            }
            if let Some(flag) = hdc_param(bin, "const.security.developermode.state") {
                developer = Some(is_true(&flag));
                device_connected = true;
            }
        }
        if developer.is_none() {
            if let Some(text) = edition.as_deref() {
                if identity == OhIdentity::OpenHarmony
                    && text.to_ascii_lowercase().contains("developer-mode")
                {
                    developer = Some(true);
                }
            }
        }
        if host == crate::model::HostPlatform::OpenHarmony {
            if let Some(bin) = probe.lookup("param") {
                if let Some(flag) = command_stdout(
                    &bin,
                    &["get", "const.security.developermode.state"],
                    Duration::from_secs(2),
                ) {
                    developer = Some(is_true(&flag));
                }
                if identity == OhIdentity::Unknown {
                    if let Some(name) = command_stdout(
                        &bin,
                        &["get", "const.product.os.dist.name"],
                        Duration::from_secs(2),
                    ) {
                        identity = identity_from_text(&name);
                    }
                }
            }
        }
        OhFacts {
            identity,
            developer_mode: developer,
            device_connected,
            hdc,
            runtime,
            bridge,
            image,
        }
    }
}

#[derive(Debug, Clone)]
pub struct FixedOpenHarmony {
    pub facts: OhFacts,
}

impl OhSource for FixedOpenHarmony {
    fn facts(
        &self,
        _host: crate::model::HostPlatform,
        _config: &Config,
        _probe: &dyn Probe,
    ) -> OhFacts {
        self.facts.clone()
    }
}

pub fn identity_from_text(text: &str) -> OhIdentity {
    let lower = text.to_ascii_lowercase();
    if lower.contains("openharmony") || lower.contains("open harmony") {
        OhIdentity::OpenHarmony
    } else if lower.contains("harmonyos") || lower.contains("harmony os") {
        OhIdentity::CommercialHarmony
    } else if lower.trim().is_empty() {
        OhIdentity::Unknown
    } else {
        OhIdentity::Other
    }
}

pub fn inspect_package(path: &Path, kind: PackageKind) -> Result<PackageInfo, Error> {
    match kind {
        PackageKind::OpenHarmonyHap => inspect_hap(path),
        PackageKind::OpenHarmonyApp => inspect_app(path),
        _ => Err(Error::Mismatch {
            message: "This is not an OpenHarmony package.".into(),
        }),
    }
}

fn inspect_hap(path: &Path) -> Result<PackageInfo, Error> {
    let entries = zipdoc::list_file(path)?;
    let signature = signature_from_names(&entries);
    let (bundle_name, ability) = read_module_fields(path, &entries, None)?;
    Ok(PackageInfo {
        kind: PackageKind::OpenHarmonyHap,
        signature,
        bundle_name,
        ability,
    })
}

fn inspect_app(path: &Path) -> Result<PackageInfo, Error> {
    let entries = zipdoc::list_file(path)?;
    let outer = signature_from_names(&entries);
    let mut signature = outer;
    let mut bundle_name = None;
    let mut ability = None;
    if outer != SignatureState::Present {
        let mut saw_hap = false;
        let mut all_signed = true;
        let mut any_unknown = false;
        for entry in entries
            .iter()
            .filter(|entry| entry.name.to_ascii_lowercase().ends_with(".hap"))
        {
            saw_hap = true;
            match zipdoc::read_entry(path, entry) {
                Ok(bytes) => match zipdoc::list_bytes(&bytes) {
                    Ok(inner) => {
                        if signature_from_names(&inner) != SignatureState::Present {
                            all_signed = false;
                        }
                        if bundle_name.is_none() {
                            if let Ok((name, found_ability)) =
                                read_module_fields_bytes(&bytes, &inner)
                            {
                                bundle_name = name;
                                ability = found_ability;
                            }
                        }
                    }
                    Err(_) => any_unknown = true,
                },
                Err(_) => any_unknown = true,
            }
        }
        signature = if !saw_hap || any_unknown {
            SignatureState::Unknown
        } else if all_signed {
            SignatureState::Present
        } else {
            SignatureState::Absent
        };
    }
    if bundle_name.is_none() {
        if let Some(pack) = entries.iter().find(|entry| entry.name == "pack.info") {
            if let Ok(bytes) = zipdoc::read_entry(path, pack) {
                if let Ok(text) = String::from_utf8(bytes) {
                    bundle_name = json_string(&text, "bundleName")
                        .filter(|value| crate::util::valid_dotted_id(value));
                }
            }
        }
    }
    Ok(PackageInfo {
        kind: PackageKind::OpenHarmonyApp,
        signature,
        bundle_name,
        ability,
    })
}

pub fn allows_install(
    signature: SignatureState,
    developer_mode: Option<bool>,
) -> Result<(), Error> {
    if signature == SignatureState::Present {
        return Ok(());
    }
    if developer_mode == Some(true) {
        return Ok(());
    }
    let why = if signature == SignatureState::Unknown {
        "Olympium could not see a signature block in this OpenHarmony package."
    } else {
        "This OpenHarmony package has no signature block."
    };
    Err(Error::Limited {
        id: "openharmony-unsigned",
        message: format!(
            "{why} Unsigned or unreadable packages install only when a real OpenHarmony runtime is in developer mode, using its official installer. Olympium does not add a certificate, remove a signature, or bypass verification."
        ),
    })
}

fn signature_from_names(entries: &[ZipEntry]) -> SignatureState {
    let signed = entries.iter().any(|entry| is_signature_file(&entry.name))
        && entries.iter().any(|entry| is_signature_block(&entry.name));
    if signed {
        SignatureState::Present
    } else {
        SignatureState::Absent
    }
}

fn is_signature_file(name: &str) -> bool {
    let lower = name.to_ascii_lowercase();
    lower.starts_with("meta-inf/") && lower.ends_with(".sf")
}

fn is_signature_block(name: &str) -> bool {
    let lower = name.to_ascii_lowercase();
    lower.starts_with("meta-inf/")
        && (lower.ends_with(".rsa")
            || lower.ends_with(".dsa")
            || lower.ends_with(".ec")
            || lower.ends_with(".p7b"))
}

fn read_module_fields(
    path: &Path,
    entries: &[ZipEntry],
    memory: Option<&[u8]>,
) -> Result<(Option<String>, Option<String>), Error> {
    let _ = memory;
    let Some(entry) = module_entry(entries) else {
        return Ok((None, None));
    };
    let bytes = zipdoc::read_entry(path, entry)?;
    Ok(fields_from_module(&bytes))
}

fn read_module_fields_bytes(
    archive: &[u8],
    entries: &[ZipEntry],
) -> Result<(Option<String>, Option<String>), Error> {
    let Some(entry) = module_entry(entries) else {
        return Ok((None, None));
    };
    let bytes = zipdoc::read_entry_bytes(archive, entry)?;
    Ok(fields_from_module(&bytes))
}

fn module_entry(entries: &[ZipEntry]) -> Option<&ZipEntry> {
    entries
        .iter()
        .find(|entry| entry.name == "module.json")
        .or_else(|| entries.iter().find(|entry| entry.name == "config.json"))
}

fn fields_from_module(bytes: &[u8]) -> (Option<String>, Option<String>) {
    let Ok(text) = std::str::from_utf8(bytes) else {
        return (None, None);
    };
    let bundle = json_string(text, "bundleName")
        .or_else(|| json_string(text, "bundle"))
        .filter(|value| crate::util::valid_dotted_id(value));
    let ability = json_string(text, "mainElement")
        .or_else(|| json_string(text, "abilityName"))
        .filter(|value| {
            !value.is_empty()
                && value.len() <= 128
                && value.chars().all(|c| c.is_ascii_alphanumeric() || c == '_')
        });
    (bundle, ability)
}

fn json_string(text: &str, field: &str) -> Option<String> {
    let pattern = format!("\"{field}\"");
    let index = text.find(&pattern)?;
    let rest = &text[index + pattern.len()..];
    let colon = rest.find(':')?;
    let after = rest[colon + 1..].trim_start();
    let after = after.strip_prefix('"')?;
    let end = after.find('"')?;
    Some(after[..end].to_string())
}

fn read_edition(dir: &Path) -> Option<String> {
    let path = dir.join("EDITION");
    let text = std::fs::read_to_string(path).ok()?;
    Some(text)
}

fn existing_file(path: &Path) -> Option<String> {
    if path.is_file() {
        Some(path.to_string_lossy().into_owned())
    } else {
        None
    }
}

fn nonempty(value: &str) -> Option<String> {
    let trimmed = value.trim();
    if trimmed.is_empty() {
        None
    } else {
        Some(trimmed.to_string())
    }
}

fn first_file(values: impl Iterator<Item = String>) -> Option<String> {
    values.into_iter().find(|value| Path::new(value).is_file())
}

fn is_true(value: &str) -> bool {
    matches!(
        value.trim().to_ascii_lowercase().as_str(),
        "true" | "1" | "on"
    )
}

fn hdc_param(hdc: &str, key: &str) -> Option<String> {
    command_stdout(hdc, &["shell", "param", "get", key], Duration::from_secs(3))
}

fn command_stdout(program: &str, args: &[&str], timeout: Duration) -> Option<String> {
    let mut child = Command::new(program)
        .args(args)
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .spawn()
        .ok()?;
    let started = Instant::now();
    loop {
        match child.try_wait() {
            Ok(Some(status)) => {
                if !status.success() {
                    return None;
                }
                let mut stdout = child.stdout.take()?;
                let mut text = String::new();
                stdout.read_to_string(&mut text).ok()?;
                let trimmed = text.trim();
                if trimmed.is_empty() || trimmed.contains("fail") {
                    return None;
                }
                return Some(trimmed.to_string());
            }
            Ok(None) if started.elapsed() > timeout => {
                let _ = child.kill();
                let _ = child.wait();
                return None;
            }
            Ok(None) => thread::sleep(Duration::from_millis(30)),
            Err(_) => return None,
        }
    }
}
