// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use crate::error::Error;
use crate::model::{GuestSystem, PackageKind, Target};
use crate::request::LaunchRequest;
use crate::util::{new_id, unix_ms};
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Catalog {
    pub version: u32,
    #[serde(default)]
    pub programs: Vec<Program>,
}

impl Default for Catalog {
    fn default() -> Self {
        Self {
            version: 1,
            programs: Vec::new(),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Program {
    pub id: String,
    pub name: String,
    pub guest: GuestSystem,
    pub kind: PackageKind,
    pub target: Target,
    #[serde(default)]
    pub args: Vec<String>,
    #[serde(default)]
    pub selector: Option<String>,
    pub added_unix_ms: u64,
}

impl Program {
    pub fn to_request(&self) -> LaunchRequest {
        LaunchRequest {
            name: self.name.clone(),
            guest: self.guest,
            kind: self.kind,
            target: self.target.clone(),
            args: self.args.clone(),
            selector: self.selector.clone(),
        }
    }
}

impl Catalog {
    pub fn add(&mut self, request: LaunchRequest) -> Program {
        let program = Program {
            id: new_id(&request.name),
            name: request.name,
            guest: request.guest,
            kind: request.kind,
            target: request.target,
            args: request.args,
            selector: request.selector,
            added_unix_ms: unix_ms(),
        };
        self.programs.push(program.clone());
        program
    }

    pub fn remove(&mut self, id: &str) -> Result<Program, Error> {
        let index = self
            .programs
            .iter()
            .position(|program| program.id == id)
            .ok_or_else(|| Error::Target {
                message: format!("No program with id {id}."),
            })?;
        Ok(self.programs.remove(index))
    }

    pub fn get(&self, id: &str) -> Option<&Program> {
        self.programs.iter().find(|program| program.id == id)
    }
}

pub fn load_catalog(path: &Path) -> Result<Catalog, Error> {
    if !path.exists() {
        return Ok(Catalog::default());
    }
    let text = fs::read_to_string(path).map_err(|err| Error::io(err, path.display()))?;
    if text.trim().is_empty() {
        return Ok(Catalog::default());
    }
    let catalog: Catalog = serde_json::from_str(&text).map_err(|err| Error::Config {
        message: format!("Could not read the library {}: {err}", path.display()),
    })?;
    if catalog.version != 1 {
        return Err(Error::Config {
            message: format!(
                "This library uses version {}, and this Olympium understands version 1.",
                catalog.version
            ),
        });
    }
    Ok(catalog)
}

pub fn save_catalog(path: &Path, catalog: &Catalog) -> Result<(), Error> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|err| Error::io(err, parent.display()))?;
    }
    let text = serde_json::to_string_pretty(catalog).map_err(|err| Error::Io {
        message: format!("Could not write the library: {err}"),
    })?;
    let tmp = path.with_extension("json.tmp");
    fs::write(&tmp, format!("{text}\n")).map_err(|err| Error::io(err, tmp.display()))?;
    fs::rename(&tmp, path).map_err(|err| Error::io(err, path.display()))?;
    Ok(())
}
