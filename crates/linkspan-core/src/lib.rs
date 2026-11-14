// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

#![forbid(unsafe_code)]

mod apple;
mod catalog;
mod classify;
mod config;
mod cpio;
mod deb;
mod doctor;
mod error;
mod export;
mod launch;
mod messages;
mod model;
mod openharmony;
mod paths;
mod policy;
mod probe;
mod request;
mod runtime;
mod session;
mod util;
mod zipdoc;

pub use apple::eject;
pub use catalog::{load_catalog, save_catalog, Catalog, Program};
pub use classify::{classify_path, is_macho, pe_machine, Classification};
pub use config::{load_config, save_config, Config};
pub use cpio::encode_newc;
pub use deb::{extract_deb, sample_deb_bytes};
pub use doctor::{doctor, DoctorReport, EngineStatus};
pub use error::Error;
pub use export::{envelope, error_envelope, export_json, share_document, ShareDocument};
pub use launch::{
    execute, prepare, prepare_with, Execution, LaunchKind, PayloadRunner, Prepared, RunOptions,
};
pub use messages::{apple_refusal, ios_package_refusal, limits, Limit};
pub use model::*;
pub use openharmony::{
    allows_install, identity_from_text, inspect_package, FixedOpenHarmony, OhFacts, OhIdentity,
    OhSource, PackageInfo, SignatureState, SystemOpenHarmony,
};
pub use paths::Layout;
pub use policy::{
    cell, cell_level, format_decision, formats, matrix, matrix_table, FormatDecision,
};
pub use probe::{MapProbe, Probe, SystemProbe};
pub use request::{
    request_for_android_package, request_for_file, request_for_flatpak, request_for_ios_bundle,
    request_for_openharmony_bundle, request_for_snap, resolve_identity, LaunchRequest,
};
pub use runtime::{
    available_memory_mib, bundled_lock, candidate_urls, choose_mirror, fetch_runtime,
    guest_runtime_id, host_arch, installed_wine, memory_budget_mib, parse_ip_body, parse_lock,
    read_mirror, runtime_rows, write_mirror, FetchReport, MirrorChoice, MirrorSource, Net,
    Progress, SystemNet,
};
pub use session::{append_session, read_sessions, SessionRecord};

pub fn version() -> &'static str {
    env!("CARGO_PKG_VERSION")
}
