// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use crate::error::Error;
use crate::model::GuestSystem;
use serde::{Deserialize, Serialize};
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::Path;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SessionRecord {
    pub at_unix_ms: u64,
    pub name: String,
    pub guest: GuestSystem,
    pub engine_id: String,
    pub ok: bool,
    pub message: String,
}

pub fn append_session(path: &Path, record: &SessionRecord) -> Result<(), Error> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|err| Error::io(err, parent.display()))?;
    }
    let mut line = serde_json::to_string(record).map_err(|err| Error::Io {
        message: format!("Could not record the session: {err}"),
    })?;
    line.push('\n');
    let mut file = OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)
        .map_err(|err| Error::io(err, path.display()))?;
    file.write_all(line.as_bytes())
        .map_err(|err| Error::io(err, path.display()))?;
    Ok(())
}

pub fn read_sessions(path: &Path, limit: usize) -> Result<Vec<SessionRecord>, Error> {
    if !path.exists() {
        return Ok(Vec::new());
    }
    let text = fs::read_to_string(path).map_err(|err| Error::io(err, path.display()))?;
    let mut records = Vec::new();
    for line in text.lines() {
        if line.trim().is_empty() {
            continue;
        }
        if let Ok(record) = serde_json::from_str::<SessionRecord>(line) {
            records.push(record);
        }
    }
    if records.len() > limit {
        records = records.split_off(records.len() - limit);
    }
    Ok(records)
}
