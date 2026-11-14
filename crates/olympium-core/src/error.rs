// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use serde_json::{json, Value};

/// A failure the user can read. Legal refusals stay distinct from missing tools.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum Error {
    #[error("{message}")]
    Legal { id: &'static str, message: String },
    #[error("{message}")]
    Limited { id: &'static str, message: String },
    #[error("{message}")]
    EngineMissing {
        engine: &'static str,
        message: String,
    },
    #[error("{message}")]
    NotConfigured {
        engine: &'static str,
        message: String,
    },
    #[error("{message}")]
    Mismatch { message: String },
    #[error("{message}")]
    Target { message: String },
    #[error("{message}")]
    WrongHost { message: String },
    #[error("{message}")]
    Config { message: String },
    #[error("{message}")]
    Failed {
        message: String,
        guest_exit_code: Option<i32>,
        stdout: String,
        stderr: String,
    },
    #[error("{message}")]
    Io { message: String },
}

impl Error {
    pub fn io(err: impl std::fmt::Display, context: impl std::fmt::Display) -> Self {
        Self::Io {
            message: format!("{context}: {err}"),
        }
    }

    pub fn exit_code(&self) -> i32 {
        match self {
            Self::Legal { .. } | Self::Limited { .. } => 2,
            Self::EngineMissing { .. } | Self::NotConfigured { .. } | Self::WrongHost { .. } => 3,
            Self::Mismatch { .. } | Self::Target { .. } => 4,
            Self::Failed { .. } | Self::Io { .. } | Self::Config { .. } => 1,
        }
    }

    pub fn kind(&self) -> &'static str {
        match self {
            Self::Legal { .. } => "legal",
            Self::Limited { .. } => "limited",
            Self::EngineMissing { .. } => "engine_missing",
            Self::NotConfigured { .. } => "not_configured",
            Self::Mismatch { .. } => "mismatch",
            Self::Target { .. } => "target",
            Self::WrongHost { .. } => "wrong_host",
            Self::Config { .. } => "config",
            Self::Failed { .. } => "guest_failed",
            Self::Io { .. } => "io",
        }
    }

    pub fn id(&self) -> Option<&str> {
        match self {
            Self::Legal { id, .. } | Self::Limited { id, .. } => Some(id),
            Self::EngineMissing { engine, .. } | Self::NotConfigured { engine, .. } => Some(engine),
            _ => None,
        }
    }

    pub fn message(&self) -> &str {
        match self {
            Self::Legal { message, .. }
            | Self::Limited { message, .. }
            | Self::EngineMissing { message, .. }
            | Self::NotConfigured { message, .. }
            | Self::Mismatch { message }
            | Self::Target { message }
            | Self::WrongHost { message }
            | Self::Config { message }
            | Self::Failed { message, .. }
            | Self::Io { message } => message,
        }
    }

    pub fn to_json(&self) -> Value {
        let mut body = json!({
            "kind": self.kind(),
            "message": self.message(),
        });
        if let Some(id) = self.id() {
            body["id"] = json!(id);
        }
        if let Self::Failed {
            guest_exit_code,
            stdout,
            stderr,
            ..
        } = self
        {
            body["guest_exit_code"] = json!(guest_exit_code);
            body["stdout"] = json!(stdout);
            body["stderr"] = json!(stderr);
        }
        body
    }
}
