// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use std::path::{Path, PathBuf};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Layout {
    pub config: PathBuf,
    pub catalog: PathBuf,
    pub sessions: PathBuf,
    pub cache: PathBuf,
    pub mounts: PathBuf,
    pub runtimes: PathBuf,
    pub mirror: PathBuf,
}

impl Layout {
    pub fn under(root: &Path) -> Self {
        Self {
            config: root.join("config.toml"),
            catalog: root.join("catalog.json"),
            sessions: root.join("sessions.jsonl"),
            cache: root.join("cache"),
            mounts: root.join("mounts.json"),
            runtimes: root.join("runtimes"),
            mirror: root.join("mirror.json"),
        }
    }

    pub fn from_env() -> Self {
        if let Some(home) = env_path("LINKSPAN_HOME") {
            return Self::under(&home);
        }
        if cfg!(target_os = "macos") && env_path("XDG_CONFIG_HOME").is_none() {
            return macos_layout();
        }
        if cfg!(target_os = "windows") && env_path("XDG_CONFIG_HOME").is_none() {
            return windows_layout();
        }
        xdg_layout()
    }
}

fn env_path(key: &str) -> Option<PathBuf> {
    let value = std::env::var_os(key)?;
    if value.is_empty() {
        None
    } else {
        Some(PathBuf::from(value))
    }
}

fn home_dir() -> PathBuf {
    env_path("HOME")
        .or_else(|| env_path("USERPROFILE"))
        .unwrap_or_else(|| PathBuf::from("."))
}

fn xdg(var: &str, fallback_under_home: &str) -> PathBuf {
    if let Some(path) = env_path(var) {
        path
    } else {
        home_dir().join(fallback_under_home)
    }
}

fn xdg_layout() -> Layout {
    let config_root = xdg("XDG_CONFIG_HOME", ".config").join("linkspan");
    let data_root = xdg("XDG_DATA_HOME", ".local/share").join("linkspan");
    let state_root = xdg("XDG_STATE_HOME", ".local/state").join("linkspan");
    let cache_root = xdg("XDG_CACHE_HOME", ".cache").join("linkspan");
    Layout {
        config: config_root.join("config.toml"),
        catalog: data_root.join("catalog.json"),
        sessions: state_root.join("sessions.jsonl"),
        cache: cache_root.clone(),
        mounts: state_root.join("mounts.json"),
        runtimes: cache_root.join("runtimes"),
        mirror: state_root.join("mirror.json"),
    }
}

fn macos_layout() -> Layout {
    let home = home_dir();
    let support = home
        .join("Library")
        .join("Application Support")
        .join("Linkspan");
    Layout {
        config: support.join("config.toml"),
        catalog: support.join("catalog.json"),
        sessions: support.join("sessions.jsonl"),
        cache: home.join("Library").join("Caches").join("Linkspan"),
        mounts: support.join("mounts.json"),
        runtimes: home
            .join("Library")
            .join("Caches")
            .join("Linkspan")
            .join("runtimes"),
        mirror: support.join("mirror.json"),
    }
}

fn windows_layout() -> Layout {
    let roaming = env_path("APPDATA").unwrap_or_else(|| home_dir().join("AppData").join("Roaming"));
    let local =
        env_path("LOCALAPPDATA").unwrap_or_else(|| home_dir().join("AppData").join("Local"));
    let root = roaming.join("Linkspan");
    let local_root = local.join("Linkspan");
    Layout {
        config: root.join("config.toml"),
        catalog: root.join("catalog.json"),
        sessions: local_root.join("sessions.jsonl"),
        cache: local_root.join("cache"),
        mounts: local_root.join("mounts.json"),
        runtimes: local_root.join("cache").join("runtimes"),
        mirror: local_root.join("mirror.json"),
    }
}
