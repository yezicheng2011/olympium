// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

pub trait Probe {
    fn lookup(&self, program: &str) -> Option<String>;
}

#[derive(Debug, Default, Clone)]
pub struct SystemProbe;

impl Probe for SystemProbe {
    fn lookup(&self, program: &str) -> Option<String> {
        which(program).map(|path| path.to_string_lossy().into_owned())
    }
}

#[derive(Debug, Default, Clone)]
pub struct MapProbe {
    programs: BTreeMap<String, String>,
}

impl MapProbe {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn with(mut self, name: &str, path: impl Into<String>) -> Self {
        self.programs.insert(name.to_string(), path.into());
        self
    }
}

impl Probe for MapProbe {
    fn lookup(&self, program: &str) -> Option<String> {
        self.programs.get(program).cloned()
    }
}

pub fn which(program: &str) -> Option<PathBuf> {
    let direct = Path::new(program);
    if program.contains('/') || program.contains('\\') {
        return if is_runnable(direct) {
            Some(direct.to_path_buf())
        } else {
            None
        };
    }
    let paths = std::env::var_os("PATH")?;
    for dir in std::env::split_paths(&paths) {
        let candidate = dir.join(program);
        if is_runnable(&candidate) {
            return Some(candidate);
        }
        #[cfg(windows)]
        {
            let exe = dir.join(format!("{program}.exe"));
            if is_runnable(&exe) {
                return Some(exe);
            }
        }
    }
    None
}

fn is_runnable(path: &Path) -> bool {
    if !path.is_file() {
        return false;
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        path.metadata()
            .map(|meta| meta.permissions().mode() & 0o111 != 0)
            .unwrap_or(false)
    }
    #[cfg(not(unix))]
    {
        true
    }
}
