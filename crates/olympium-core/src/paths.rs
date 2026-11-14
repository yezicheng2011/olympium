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
        // OLYMPIUM_HOME is the current name. LINKSPAN_HOME still selects a
        // directory for installs made by the 0.1.0 package.
        if let Some(home) = env_path("OLYMPIUM_HOME").or_else(|| env_path("LINKSPAN_HOME")) {
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

fn named_xdg(name: &str) -> Layout {
    let config_root = xdg("XDG_CONFIG_HOME", ".config").join(name);
    let data_root = xdg("XDG_DATA_HOME", ".local/share").join(name);
    let state_root = xdg("XDG_STATE_HOME", ".local/state").join(name);
    let cache_root = xdg("XDG_CACHE_HOME", ".cache").join(name);
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

fn prefer_existing(current: Layout, legacy: Layout) -> Layout {
    if current.config.is_file() || !legacy.config.is_file() {
        current
    } else {
        legacy
    }
}

fn xdg_layout() -> Layout {
    prefer_existing(named_xdg("olympium"), named_xdg("linkspan"))
}

fn macos_named(name: &str) -> Layout {
    let home = home_dir();
    let support = home
        .join("Library")
        .join("Application Support")
        .join(name);
    let cache = home.join("Library").join("Caches").join(name);
    Layout {
        config: support.join("config.toml"),
        catalog: support.join("catalog.json"),
        sessions: support.join("sessions.jsonl"),
        cache: cache.clone(),
        mounts: support.join("mounts.json"),
        runtimes: cache.join("runtimes"),
        mirror: support.join("mirror.json"),
    }
}

fn macos_layout() -> Layout {
    prefer_existing(macos_named("Olympium"), macos_named("Linkspan"))
}

fn windows_layout() -> Layout {
    let roaming = env_path("APPDATA").unwrap_or_else(|| home_dir().join("AppData").join("Roaming"));
    let local =
        env_path("LOCALAPPDATA").unwrap_or_else(|| home_dir().join("AppData").join("Local"));
    prefer_existing(
        windows_named(&roaming, &local, "Olympium"),
        windows_named(&roaming, &local, "Linkspan"),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    #[test]
    fn an_existing_linkspan_config_is_kept_until_an_olympium_config_exists() {
        let root = std::env::temp_dir().join(format!("olympium-paths-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        let legacy = root.join("linkspan");
        let current = root.join("olympium");
        fs::create_dir_all(&legacy).unwrap();
        fs::write(legacy.join("config.toml"), "legacy = true\n").unwrap();

        let chosen = prefer_existing(Layout::under(&current), Layout::under(&legacy));
        assert_eq!(chosen.config, legacy.join("config.toml"));

        fs::create_dir_all(&current).unwrap();
        fs::write(current.join("config.toml"), "current = true\n").unwrap();
        let chosen = prefer_existing(Layout::under(&current), Layout::under(&legacy));
        assert_eq!(chosen.config, current.join("config.toml"));
        let _ = fs::remove_dir_all(&root);
    }
}

fn windows_named(roaming: &Path, local: &Path, name: &str) -> Layout {
    let root = roaming.join(name);
    let local_root = local.join(name);
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
