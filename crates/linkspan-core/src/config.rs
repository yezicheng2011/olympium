// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use crate::error::Error;
use crate::model::Appearance;
use crate::util::{reject_bad_text, valid_simple_name};
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct Config {
    pub appearance: Appearance,
    pub engines: Engines,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            appearance: Appearance::System,
            engines: Engines::default(),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(default, deny_unknown_fields)]
pub struct Engines {
    pub android: AndroidEngine,
    pub windows: WindowsEngine,
    pub wsl: WslEngine,
    pub lima: LimaEngine,
    pub linux_hook: Hook,
    pub windows_hook: Hook,
    pub openharmony: OpenHarmonyEngine,
    pub mirrors: Mirrors,
    pub experience: Experience,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct AndroidEngine {
    /// `auto`, `waydroid`, or `adb`.
    pub prefer: String,
    pub waydroid: String,
    pub adb: String,
}

impl Default for AndroidEngine {
    fn default() -> Self {
        Self {
            prefer: "auto".into(),
            waydroid: String::new(),
            adb: String::new(),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct WindowsEngine {
    /// `auto`, `wine`, or `proton`.
    pub prefer: String,
    pub wine: String,
    pub proton: String,
}

impl Default for WindowsEngine {
    fn default() -> Self {
        Self {
            prefer: "auto".into(),
            wine: String::new(),
            proton: String::new(),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(default, deny_unknown_fields)]
pub struct WslEngine {
    pub distro: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct LimaEngine {
    pub instance: String,
    pub prefix: Vec<String>,
}

impl Default for LimaEngine {
    fn default() -> Self {
        Self {
            instance: "default".into(),
            prefix: Vec::new(),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(default, deny_unknown_fields)]
pub struct Hook {
    pub prefix: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(default, deny_unknown_fields)]
pub struct OpenHarmonyEngine {
    pub hdc: String,
    pub image: String,
    pub bridge: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct Mirrors {
    /// `auto`, `github`, or `china`.
    pub prefer: String,
    pub ip_lookup_url: String,
    pub probe_url: String,
    pub china_prefix: String,
    pub github_prefix: String,
}

impl Default for Mirrors {
    fn default() -> Self {
        Self {
            prefer: "auto".into(),
            ip_lookup_url: "https://ipinfo.io/json".into(),
            probe_url: "https://github.com".into(),
            china_prefix: "https://gh-proxy.com/".into(),
            github_prefix: String::new(),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct Experience {
    /// `auto`, `host`, or `software`.
    pub graphics: String,
    /// `auto` or `off`.
    pub camera: String,
    /// `auto` or `off`.
    pub gamepad: String,
    pub quick_boot: bool,
    /// `auto` or `manual`.
    pub memory_mode: String,
    pub memory_min_mib: u64,
    pub memory_max_mib: u64,
    pub memory_manual_mib: u64,
    pub shared_folders: Vec<String>,
}

impl Default for Experience {
    fn default() -> Self {
        Self {
            graphics: "auto".into(),
            camera: "auto".into(),
            gamepad: "auto".into(),
            quick_boot: true,
            memory_mode: "auto".into(),
            memory_min_mib: 2048,
            memory_max_mib: 8192,
            memory_manual_mib: 4096,
            shared_folders: Vec::new(),
        }
    }
}

pub fn load_config(path: &Path) -> Result<Config, Error> {
    if !path.exists() {
        return Ok(Config::default());
    }
    let text = fs::read_to_string(path).map_err(|err| Error::io(err, path.display()))?;
    if text.trim().is_empty() {
        return Ok(Config::default());
    }
    let config: Config = toml::from_str(&text).map_err(|err| Error::Config {
        message: format!("Could not read {}: {err}", path.display()),
    })?;
    validate(&config)?;
    Ok(config)
}

pub fn save_config(path: &Path, config: &Config) -> Result<(), Error> {
    validate(config)?;
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|err| Error::io(err, parent.display()))?;
    }
    let text = toml::to_string_pretty(config).map_err(|err| Error::Config {
        message: format!("Could not write config: {err}"),
    })?;
    let tmp = path.with_extension("toml.tmp");
    fs::write(&tmp, text).map_err(|err| Error::io(err, tmp.display()))?;
    fs::rename(&tmp, path).map_err(|err| Error::io(err, path.display()))?;
    Ok(())
}

pub fn validate(config: &Config) -> Result<(), Error> {
    one_of(
        "engines.android.prefer",
        &config.engines.android.prefer,
        &["auto", "waydroid", "adb"],
    )?;
    one_of(
        "engines.windows.prefer",
        &config.engines.windows.prefer,
        &["auto", "wine", "proton"],
    )?;
    if !config.engines.wsl.distro.is_empty() && !valid_simple_name(&config.engines.wsl.distro) {
        return Err(Error::Config {
            message: "engines.wsl.distro may contain only letters, digits, dots, hyphens, and underscores.".into(),
        });
    }
    if !valid_simple_name(&config.engines.lima.instance) {
        return Err(Error::Config {
            message: "engines.lima.instance may contain only letters, digits, dots, hyphens, and underscores.".into(),
        });
    }
    validate_prefix("engines.lima.prefix", &config.engines.lima.prefix)?;
    validate_prefix(
        "engines.linux_hook.prefix",
        &config.engines.linux_hook.prefix,
    )?;
    validate_prefix(
        "engines.windows_hook.prefix",
        &config.engines.windows_hook.prefix,
    )?;
    one_of(
        "engines.mirrors.prefer",
        &config.engines.mirrors.prefer,
        &["auto", "github", "china"],
    )?;
    one_of(
        "engines.experience.graphics",
        &config.engines.experience.graphics,
        &["auto", "host", "software"],
    )?;
    one_of(
        "engines.experience.camera",
        &config.engines.experience.camera,
        &["auto", "off"],
    )?;
    one_of(
        "engines.experience.gamepad",
        &config.engines.experience.gamepad,
        &["auto", "off"],
    )?;
    one_of(
        "engines.experience.memory_mode",
        &config.engines.experience.memory_mode,
        &["auto", "manual"],
    )?;
    if config.engines.experience.memory_min_mib < 256
        || config.engines.experience.memory_max_mib < config.engines.experience.memory_min_mib
    {
        return Err(Error::Config {
            message:
                "memory_min_mib must be at least 256, and memory_max_mib must be at least that."
                    .into(),
        });
    }
    for url in [
        &config.engines.mirrors.ip_lookup_url,
        &config.engines.mirrors.probe_url,
        &config.engines.mirrors.china_prefix,
        &config.engines.mirrors.github_prefix,
    ] {
        validate_mirror_url(url)?;
    }
    for folder in &config.engines.experience.shared_folders {
        reject_bad_text("shared folder", folder)?;
        crate::util::reject_url(folder)?;
        if folder.starts_with('-') {
            return Err(Error::Config {
                message: "A shared folder cannot start with a dash.".into(),
            });
        }
    }
    for path in [
        &config.engines.android.waydroid,
        &config.engines.android.adb,
        &config.engines.windows.wine,
        &config.engines.windows.proton,
        &config.engines.openharmony.hdc,
        &config.engines.openharmony.image,
        &config.engines.openharmony.bridge,
    ] {
        reject_bad_text("engine path", path)?;
    }
    Ok(())
}

fn one_of(field: &str, value: &str, allowed: &[&str]) -> Result<(), Error> {
    if allowed.contains(&value) {
        Ok(())
    } else {
        Err(Error::Config {
            message: format!("{field} must be one of: {}.", allowed.join(", ")),
        })
    }
}

fn validate_mirror_url(value: &str) -> Result<(), Error> {
    if value.is_empty() {
        return Ok(());
    }
    reject_bad_text("mirror url", value)?;
    let lower = value.trim().to_ascii_lowercase();
    if !lower.starts_with("https://") {
        return Err(Error::Config {
            message: "Mirror and lookup URLs must start with https://.".into(),
        });
    }
    Ok(())
}

fn validate_prefix(field: &str, prefix: &[String]) -> Result<(), Error> {
    for part in prefix {
        reject_bad_text(field, part)?;
        if part.is_empty() {
            return Err(Error::Config {
                message: format!("{field} contains an empty argument."),
            });
        }
    }
    Ok(())
}
