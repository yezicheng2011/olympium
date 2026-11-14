// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

#![forbid(unsafe_code)]

use clap::{Parser, Subcommand};
use linkspan_core::{
    append_session, bundled_lock, classify_path, doctor, envelope, error_envelope, execute,
    export_json, fetch_runtime, host_arch, load_catalog, load_config, matrix_table, prepare,
    read_sessions, request_for_android_package, request_for_file, request_for_flatpak,
    request_for_ios_bundle, request_for_openharmony_bundle, request_for_snap, runtime_rows,
    save_catalog, save_config, Appearance, Error, GuestSystem, HostPlatform, LaunchKind,
    MirrorSource, PackageKind, RunOptions, SessionRecord, SystemNet, SystemProbe,
};
use serde_json::json;
use std::path::PathBuf;
use std::process::Command;

#[derive(Parser)]
#[command(
    name = "linkspan",
    version,
    about = "Run a program from another system on this computer",
    long_about = "Linkspan launches programs that belong to Android, Windows, Linux, macOS, iOS, or OpenHarmony.\nIt refuses macOS and iOS software on non-Apple hardware.\nOpenHarmony packages run on a real OpenHarmony runtime, not a translation layer.\nIts own code is Apache-2.0 only."
)]
struct Cli {
    /// Print machine-readable JSON on stdout.
    #[arg(long, global = true)]
    json: bool,
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Show which engines are available on this computer.
    Doctor,
    /// Show what each host can run.
    Matrix,
    /// Show the legal and technical limits.
    Limits,
    /// List programs in the library.
    List,
    /// Add a program to the library.
    Add(SourceArgs),
    /// Remove a program from the library.
    Remove { id: String },
    /// Show how a program would be started.
    Plan(RunArgs),
    /// Start a program.
    Run(RunArgs),
    /// Say what Linkspan thinks a file is.
    Classify { file: PathBuf },
    /// Show recent launches.
    Sessions,
    /// Read or change host settings.
    Config {
        #[command(subcommand)]
        action: ConfigCmd,
    },
    /// Open the Linux desktop app, when it is installed beside this command.
    Ui,
    /// Print the compatibility document used by the other host apps.
    Share,
    /// Show which download source the client IP selects, and the slim images.
    Runtime,
    /// Download one slim guest runtime. Images are not stored in Linkspan.
    Fetch {
        /// `android`, `linux`, `openharmony`, or `windows` (the slim Wine runtime).
        #[arg(long)]
        guest: String,
    },
}

#[derive(Subcommand)]
enum ConfigCmd {
    /// Print the current settings.
    Get,
    /// Change one setting. Only `appearance` can be set from here.
    Set { key: String, value: String },
}

#[derive(clap::Args, Clone)]
struct SourceArgs {
    #[arg(long)]
    file: Option<PathBuf>,
    #[arg(long)]
    name: Option<String>,
    #[arg(long, value_parser = parse_kind)]
    kind: Option<PackageKind>,
    #[arg(long, value_parser = parse_guest)]
    guest: Option<GuestSystem>,
    #[arg(long)]
    bin: Option<String>,
    #[arg(long)]
    package: Option<String>,
    #[arg(long)]
    bundle_id: Option<String>,
    #[arg(long)]
    android_package: Option<String>,
    #[arg(long)]
    ios_bundle: Option<String>,
    #[arg(long)]
    flatpak: Option<String>,
    #[arg(long)]
    snap: Option<String>,
    #[arg(long)]
    openharmony_bundle: Option<String>,
}

#[derive(clap::Args, Clone)]
struct RunArgs {
    /// Library id. Omit this when you pass --file or another source.
    id: Option<String>,
    #[command(flatten)]
    source: SourceArgs,
    /// Keep the program's output instead of letting it use the terminal.
    #[arg(long)]
    capture: bool,
    /// Return as soon as the program has started.
    #[arg(long)]
    detach: bool,
    /// Arguments for the program. Put them after --.
    #[arg(trailing_var_arg = true, allow_hyphen_values = true)]
    args: Vec<String>,
}

fn parse_kind(value: &str) -> Result<PackageKind, String> {
    PackageKind::parse(value).ok_or_else(|| format!("unknown package kind `{value}`"))
}

fn parse_guest(value: &str) -> Result<GuestSystem, String> {
    let lower = value.trim().to_ascii_lowercase();
    if matches!(lower.as_str(), "ios-host" | "iphone-host") {
        return Err("Linkspan has no iOS host. iOS apps are guests.".into());
    }
    if GuestSystem::is_commercial_harmony_name(&lower) {
        return Err(
            "HarmonyOS commercial images are not a guest. Use openharmony for OpenHarmony packages."
                .into(),
        );
    }
    GuestSystem::parse(&lower).ok_or_else(|| format!("unknown system `{value}`"))
}

fn main() {
    let cli = Cli::parse();
    let code = match dispatch(cli.json, cli.command) {
        Ok(code) => code,
        Err(err) => {
            report(cli.json, "linkspan", &err);
            err.exit_code()
        }
    };
    std::process::exit(code);
}

fn dispatch(json: bool, command: Commands) -> Result<i32, Error> {
    let host = HostPlatform::current();
    let layout = linkspan_core::Layout::from_env();
    match command {
        Commands::Doctor => {
            let config = load_config(&layout.config)?;
            let report = doctor(host, &config, &SystemProbe, &layout);
            emit(json, "doctor", host, json!(report), &doctor_text(&report))
        }
        Commands::Matrix => emit(
            json,
            "matrix",
            host,
            json!(linkspan_core::matrix()),
            &matrix_table(),
        ),
        Commands::Limits => {
            let limits = linkspan_core::limits();
            let mut text = String::new();
            for limit in limits {
                text.push_str(&format!("{}\n{}\n\n", limit.title, limit.body));
            }
            emit(json, "limits", host, json!(limits), &text)
        }
        Commands::List => {
            let catalog = load_catalog(&layout.catalog)?;
            emit(json, "list", host, json!(catalog), &list_text(&catalog))
        }
        Commands::Add(source) => {
            let mut request = request_from_source(&source, Vec::new())?;
            if let Some(name) = source.name.clone() {
                request.name = name;
            }
            let mut catalog = load_catalog(&layout.catalog)?;
            let program = catalog.add(request);
            save_catalog(&layout.catalog, &catalog)?;
            emit(
                json,
                "add",
                host,
                json!(program),
                &format!("Added {} ({})\n", program.name, program.id),
            )
        }
        Commands::Remove { id } => {
            let mut catalog = load_catalog(&layout.catalog)?;
            let program = catalog.remove(&id)?;
            save_catalog(&layout.catalog, &catalog)?;
            emit(
                json,
                "remove",
                host,
                json!(program),
                &format!("Removed {} ({})\n", program.name, program.id),
            )
        }
        Commands::Classify { file } => {
            let class = classify_path(&file)?;
            emit(
                json,
                "classify",
                host,
                json!(class),
                &format!(
                    "{}: {} ({})\n",
                    class.kind.label(),
                    class.detail,
                    class.guest.label()
                ),
            )
        }
        Commands::Plan(args) => cmd_plan(json, host, &layout, args),
        Commands::Run(args) => cmd_run(json, host, &layout, args),
        Commands::Sessions => {
            let sessions = read_sessions(&layout.sessions, 50)?;
            let mut text = String::new();
            if sessions.is_empty() {
                text.push_str("No launches yet.\n");
            }
            for session in &sessions {
                text.push_str(&format!(
                    "{} {} {} {}\n",
                    if session.ok { "ok" } else { "stopped" },
                    session.guest.label(),
                    session.name,
                    session.message
                ));
            }
            emit(
                json,
                "sessions",
                host,
                json!({ "sessions": sessions }),
                &text,
            )
        }
        Commands::Config { action } => cmd_config(json, host, &layout, action),
        Commands::Ui => cmd_ui(),
        Commands::Share => {
            let document: serde_json::Value =
                serde_json::from_str(&export_json()).map_err(|err| Error::Io {
                    message: format!("could not read the compatibility document: {err}"),
                })?;
            emit(json, "share", host, document, &export_json())
        }
        Commands::Runtime => cmd_runtime(json, host, &layout),
        Commands::Fetch { guest } => cmd_fetch(json, host, &layout, &guest),
    }
}

fn cmd_plan(
    json: bool,
    host: HostPlatform,
    layout: &linkspan_core::Layout,
    args: RunArgs,
) -> Result<i32, Error> {
    let config = load_config(&layout.config)?;
    let request = request_from_run(layout, &args)?;
    let prepared = prepare(host, &request, &config, &SystemProbe)?;
    emit(json, "plan", host, json!(prepared), &plan_text(&prepared))
}

fn cmd_run(
    json: bool,
    host: HostPlatform,
    layout: &linkspan_core::Layout,
    args: RunArgs,
) -> Result<i32, Error> {
    if args.detach && (json || args.capture) {
        return Err(Error::Target {
            message: "Detach cannot be combined with --json or --capture.".into(),
        });
    }
    let config = load_config(&layout.config)?;
    let request = request_from_run(layout, &args)?;
    let name = request.name.clone();
    let guest = request.guest;
    let prepared = match prepare(host, &request, &config, &SystemProbe) {
        Ok(prepared) => prepared,
        Err(err) => {
            remember(layout, &name, guest, "", false, err.message());
            return Err(err);
        }
    };
    let options = RunOptions {
        capture: json || args.capture,
        detach: args.detach,
    };
    if !json && !args.capture {
        println!("Running {name} with {}.", prepared.engine_id);
    }
    match execute(&prepared, &options, layout) {
        Ok(execution) => {
            remember(
                layout,
                &name,
                guest,
                &execution.engine_id,
                true,
                &execution.summary,
            );
            if execution.detached_pid.is_some() {
                emit(
                    json,
                    "run",
                    host,
                    json!(execution),
                    &format!("Process id: {}\n", execution.detached_pid.unwrap_or(0)),
                )
            } else if json || args.capture {
                emit(json, "run", host, json!(execution), &execution.stdout)
            } else {
                println!("Exit code: {}", execution.guest_exit_code.unwrap_or(0));
                Ok(0)
            }
        }
        Err(err) => {
            remember(
                layout,
                &name,
                guest,
                prepared.engine_id,
                false,
                err.message(),
            );
            Err(err)
        }
    }
}

fn cmd_config(
    json: bool,
    host: HostPlatform,
    layout: &linkspan_core::Layout,
    action: ConfigCmd,
) -> Result<i32, Error> {
    match action {
        ConfigCmd::Get => {
            let config = load_config(&layout.config)?;
            emit(
                json,
                "config",
                host,
                json!({ "appearance": config.appearance, "path": layout.config.display().to_string() }),
                &format!(
                    "appearance = {}\nconfig = {}\n",
                    config.appearance.as_str(),
                    layout.config.display()
                ),
            )
        }
        ConfigCmd::Set { key, value } => {
            let mut config = load_config(&layout.config)?;
            let shown = value.clone();
            match key.as_str() {
                "appearance" => {
                    config.appearance = Appearance::parse(&value).ok_or_else(|| Error::Config {
                        message: "appearance must be system, light, or dark.".into(),
                    })?;
                }
                "mirrors.prefer" => {
                    if !matches!(value.as_str(), "auto" | "github" | "china") {
                        return Err(Error::Config {
                            message: "mirrors.prefer must be auto, github, or china.".into(),
                        });
                    }
                    config.engines.mirrors.prefer = value;
                }
                "experience.graphics" => {
                    if !matches!(value.as_str(), "auto" | "host" | "software") {
                        return Err(Error::Config {
                            message: "experience.graphics must be auto, host, or software.".into(),
                        });
                    }
                    config.engines.experience.graphics = value;
                }
                "experience.camera" | "experience.gamepad" => {
                    if !matches!(value.as_str(), "auto" | "off") {
                        return Err(Error::Config {
                            message: format!("{key} must be auto or off."),
                        });
                    }
                    if key.ends_with("camera") {
                        config.engines.experience.camera = value;
                    } else {
                        config.engines.experience.gamepad = value;
                    }
                }
                "experience.memory.mode" => {
                    if !matches!(value.as_str(), "auto" | "manual") {
                        return Err(Error::Config {
                            message: "experience.memory.mode must be auto or manual.".into(),
                        });
                    }
                    config.engines.experience.memory_mode = value;
                }
                "experience.quick_boot" => {
                    config.engines.experience.quick_boot = match value.as_str() {
                        "on" | "true" => true,
                        "off" | "false" => false,
                        _ => {
                            return Err(Error::Config {
                                message: "experience.quick_boot must be on or off.".into(),
                            });
                        }
                    };
                }
                _ => {
                    return Err(Error::Config {
                        message: "That setting is not changed from the command. You can set appearance, mirrors.prefer, experience.graphics, experience.camera, experience.gamepad, experience.memory.mode, and experience.quick_boot.".into(),
                    });
                }
            }
            save_config(&layout.config, &config)?;
            emit(
                json,
                "config",
                host,
                json!({ "key": key, "value": shown }),
                &format!("{key} = {shown}\n"),
            )
        }
    }
}

fn cmd_runtime(
    json: bool,
    host: HostPlatform,
    layout: &linkspan_core::Layout,
) -> Result<i32, Error> {
    let config = load_config(&layout.config)?;
    let lock = bundled_lock();
    let choice = linkspan_core::choose_mirror(&config.engines.mirrors, &SystemNet)?;
    linkspan_core::write_mirror(layout, &choice)?;
    let rows = runtime_rows(&lock, layout);
    let source = match choice.source {
        MirrorSource::Github => "github",
        MirrorSource::China => "china",
    };
    let mut text = format!("Download source: {source} ({})\n", choice.reason);
    if let Some(ip) = &choice.client_ip {
        text.push_str(&format!("Client IP: {ip}\n"));
    }
    text.push('\n');
    for row in &rows {
        text.push_str(row);
        text.push('\n');
    }
    emit(
        json,
        "runtime",
        host,
        json!({ "mirror": choice, "runtimes": rows }),
        &text,
    )
}

fn cmd_fetch(
    json: bool,
    host: HostPlatform,
    layout: &linkspan_core::Layout,
    guest: &str,
) -> Result<i32, Error> {
    let id = match guest {
        "android" => "android",
        "linux" => "linux",
        "openharmony" | "harmony" => "openharmony",
        "windows" | "wine" => "wine",
        "macos" | "ios" => {
            return Err(Error::Legal {
                id: "apple-image",
                message: "Linkspan does not download a macOS or iOS system image.".into(),
            });
        }
        "harmonyos" => {
            return Err(Error::Legal {
                id: "commercial-harmonyos",
                message: "HarmonyOS commercial images are not downloaded. Use --guest openharmony."
                    .into(),
            });
        }
        other => {
            return Err(Error::Target {
                message: format!(
                    "Unknown runtime `{other}`. Use android, linux, openharmony, or windows."
                ),
            });
        }
    };
    let mut config = load_config(&layout.config)?;
    let lock = bundled_lock();
    let report = fetch_runtime(
        id,
        &lock,
        &config.engines.mirrors,
        &SystemNet,
        layout,
        host_arch(),
        |progress| {
            if !json {
                if let Some(total) = progress.total {
                    eprintln!("downloaded {} / {total}", progress.received);
                } else {
                    eprintln!("downloaded {}", progress.received);
                }
            }
        },
    )?;
    if id == "openharmony" {
        if let Some(path) = &report.path {
            config.engines.openharmony.image = path.clone();
            save_config(&layout.config, &config)?;
        }
    }
    if report.outcome == "not_published" {
        return Err(Error::NotConfigured {
            engine: "runtime",
            message: report.message,
        });
    }
    emit(
        json,
        "fetch",
        host,
        json!(report),
        &format!("{}\n", report.message),
    )
}

fn cmd_ui() -> Result<i32, Error> {
    let sibling = std::env::current_exe()
        .ok()
        .and_then(|path| path.parent().map(|dir| dir.join("linkspan-linux")));
    let status = if let Some(path) = sibling.as_ref().filter(|path| path.is_file()) {
        Command::new(path).status()
    } else {
        Command::new("linkspan-linux").status()
    };
    match status {
        Ok(status) if status.success() => Ok(0),
        Ok(status) => Err(Error::Failed {
            message: "The desktop app closed with an error.".into(),
            guest_exit_code: status.code(),
            stdout: String::new(),
            stderr: String::new(),
        }),
        Err(_) => Err(Error::EngineMissing {
            engine: "native",
            message: "The Linux desktop app is not installed. Build it with `cargo build -p linkspan-linux` from a machine that has GTK 4 and libadwaita.".into(),
        }),
    }
}

fn request_from_run(
    layout: &linkspan_core::Layout,
    args: &RunArgs,
) -> Result<linkspan_core::LaunchRequest, Error> {
    if let Some(id) = &args.id {
        if source_is_set(&args.source) {
            return Err(Error::Target {
                message: "Pass a library id or a file, not both.".into(),
            });
        }
        let catalog = load_catalog(&layout.catalog)?;
        let mut request = catalog
            .get(id)
            .ok_or_else(|| Error::Target {
                message: format!("No program with id {id}."),
            })?
            .to_request();
        if !args.args.is_empty() {
            request.args = args.args.clone();
        }
        return Ok(request);
    }
    request_from_source(&args.source, args.args.clone())
}

fn request_from_source(
    source: &SourceArgs,
    args: Vec<String>,
) -> Result<linkspan_core::LaunchRequest, Error> {
    let selector = one_selector(&source.bin, &source.package, &source.bundle_id)?;
    let mut chosen = 0;
    if source.file.is_some() {
        chosen += 1;
    }
    if source.android_package.is_some() {
        chosen += 1;
    }
    if source.ios_bundle.is_some() {
        chosen += 1;
    }
    if source.flatpak.is_some() {
        chosen += 1;
    }
    if source.snap.is_some() {
        chosen += 1;
    }
    if source.openharmony_bundle.is_some() {
        chosen += 1;
    }
    if chosen != 1 {
        return Err(Error::Target {
            message: "Choose one of --file, --android-package, --ios-bundle, --openharmony-bundle, --flatpak, or --snap.".into(),
        });
    }
    let name = source.name.as_deref();
    if let Some(file) = &source.file {
        return request_for_file(file, name, source.kind, source.guest, selector, args);
    }
    if let Some(package) = &source.android_package {
        return request_for_android_package(package, name, args);
    }
    if let Some(bundle) = &source.ios_bundle {
        return request_for_ios_bundle(bundle, name, args);
    }
    if let Some(reference) = &source.flatpak {
        return request_for_flatpak(reference, name, args);
    }
    if let Some(name_snap) = &source.snap {
        return request_for_snap(name_snap, name, args);
    }
    if let Some(bundle) = &source.openharmony_bundle {
        return request_for_openharmony_bundle(bundle, name, args);
    }
    Err(Error::Target {
        message: "Nothing to launch.".into(),
    })
}

fn one_selector(
    bin: &Option<String>,
    package: &Option<String>,
    bundle: &Option<String>,
) -> Result<Option<String>, Error> {
    let values: Vec<String> = [bin, package, bundle]
        .into_iter()
        .filter_map(|value| value.clone())
        .collect();
    if values.len() > 1 {
        return Err(Error::Target {
            message: "Use only one of --bin, --package, and --bundle-id.".into(),
        });
    }
    Ok(values.into_iter().next())
}

fn source_is_set(source: &SourceArgs) -> bool {
    source.file.is_some()
        || source.android_package.is_some()
        || source.ios_bundle.is_some()
        || source.flatpak.is_some()
        || source.snap.is_some()
        || source.openharmony_bundle.is_some()
}

fn remember(
    layout: &linkspan_core::Layout,
    name: &str,
    guest: GuestSystem,
    engine: &str,
    ok: bool,
    message: &str,
) {
    let _ = append_session(
        &layout.sessions,
        &SessionRecord {
            at_unix_ms: std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|duration| duration.as_millis() as u64)
                .unwrap_or(0),
            name: name.to_string(),
            guest,
            engine_id: engine.to_string(),
            ok,
            message: message.chars().take(400).collect(),
        },
    );
}

fn emit(
    json_mode: bool,
    command: &str,
    host: HostPlatform,
    data: serde_json::Value,
    human: &str,
) -> Result<i32, Error> {
    if json_mode {
        let text = serde_json::to_string_pretty(&envelope(true, command, host, Some(data), None))
            .map_err(|err| Error::Io {
            message: format!("could not encode JSON: {err}"),
        })?;
        println!("{text}");
    } else {
        print!("{human}");
        if !human.ends_with('\n') {
            println!();
        }
    }
    Ok(0)
}

fn report(json_mode: bool, command: &str, err: &Error) {
    if json_mode {
        let text =
            serde_json::to_string_pretty(&error_envelope(command, HostPlatform::current(), err))
                .unwrap_or_else(|_| "{\"ok\":false}".into());
        println!("{text}");
    } else {
        eprintln!("error: {}", err.message());
        if let Error::Failed { stdout, stderr, .. } = err {
            if !stdout.is_empty() {
                eprintln!("{stdout}");
            }
            if !stderr.is_empty() {
                eprintln!("{stderr}");
            }
        }
    }
}

fn doctor_text(report: &linkspan_core::DoctorReport) -> String {
    let mut text = format!(
        "Linkspan {} on {}\nConfig: {}\nLibrary: {}\n\n",
        report.version,
        report.host.label(),
        report.config_path,
        report.catalog_path
    );
    for engine in &report.engines {
        if !engine.applicable {
            continue;
        }
        let where_ = engine.location.as_deref().unwrap_or("-");
        text.push_str(&format!(
            "{:<16} {:<14} {}\n    {}\n",
            engine.id, engine.state, where_, engine.detail
        ));
    }
    text
}

fn list_text(catalog: &linkspan_core::Catalog) -> String {
    if catalog.programs.is_empty() {
        return "No programs yet. Add one with `linkspan add --file <path>`.\n".into();
    }
    let mut text = String::new();
    for program in &catalog.programs {
        text.push_str(&format!(
            "{}\t{}\t{}\t{}\n",
            program.id,
            program.guest.as_str(),
            program.kind.as_str(),
            program.name
        ));
    }
    text
}

fn plan_text(prepared: &linkspan_core::Prepared) -> String {
    let mut text = format!(
        "{}\nGuest: {}\nEngine: {}\n{}\n",
        prepared.name,
        prepared.guest.label(),
        prepared.engine_id,
        prepared.summary
    );
    for note in &prepared.notes {
        text.push_str(&format!("Note: {note}\n"));
    }
    text.push_str(&format!("Launch: {}\n", launch_brief(&prepared.launch)));
    text
}

fn launch_brief(launch: &LaunchKind) -> String {
    match launch {
        LaunchKind::Process { program, args, .. } => format!("{program} {}", args.join(" ")),
        LaunchKind::Sequence { steps } => format!("{} steps", steps.len()),
        LaunchKind::ExtractThenRun { format, .. } => {
            format!("extract a {format:?} package, then run the program inside it")
        }
        LaunchKind::AndroidIntent { package } => format!("open Android package {package}"),
        LaunchKind::AndroidInstall { apk, .. } => format!("ask Android to install {apk}"),
        LaunchKind::OpenDiskImage { path } => format!("open the macOS disk image {path}"),
        LaunchKind::InstallPackage { path } => format!("run the macOS installer {path}"),
    }
}
