// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

#![allow(clippy::field_reassign_with_default)]

use olympium_core::*;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

fn scratch() -> (PathBuf, Layout) {
    static N: AtomicU64 = AtomicU64::new(0);
    let path = std::env::temp_dir().join(format!(
        "olympium-behavior-{}-{}",
        std::process::id(),
        N.fetch_add(1, Ordering::Relaxed)
    ));
    fs::create_dir_all(&path).unwrap();
    let layout = Layout::under(&path);
    (path, layout)
}

fn script(path: &Path, body: &str) {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).unwrap();
    }
    fs::write(path, body).unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(path, fs::Permissions::from_mode(0o755)).unwrap();
    }
}

fn capture() -> RunOptions {
    RunOptions {
        capture: true,
        detach: false,
    }
}

fn file_request(path: &str, kind: PackageKind) -> LaunchRequest {
    LaunchRequest {
        name: "sample".into(),
        guest: kind.guest(),
        kind,
        target: Target::File {
            path: path.to_string(),
        },
        args: Vec::new(),
        selector: None,
    }
}

#[test]
fn matrix_covers_every_host_and_guest() {
    let cells = matrix();
    assert_eq!(cells.len(), 30);
    for host in HostPlatform::ALL {
        for guest in GuestSystem::ALL {
            let cell = cell(host, guest);
            if guest.is_apple() && host != HostPlatform::MacOs {
                assert_eq!(cell.level, SupportLevel::Refused, "{host} {guest}");
                assert!(cell
                    .legal_note
                    .contains("Olympium will not run macOS or iOS software"));
            }
        }
    }
    assert_eq!(
        cell(HostPlatform::Linux, GuestSystem::Linux).level,
        SupportLevel::Ready
    );
    assert_eq!(
        cell(HostPlatform::MacOs, GuestSystem::MacOs).level,
        SupportLevel::Ready
    );
    assert_eq!(
        cell(HostPlatform::MacOs, GuestSystem::Ios).level,
        SupportLevel::Limited
    );
    assert_eq!(
        cell(HostPlatform::Android, GuestSystem::Windows).level,
        SupportLevel::Scaffold
    );
    assert_eq!(
        cell(HostPlatform::Linux, GuestSystem::Windows).level,
        SupportLevel::Engine
    );
    assert_eq!(
        cell(HostPlatform::Linux, GuestSystem::Windows).engine_id,
        Some("wine")
    );
    assert_eq!(
        cell(HostPlatform::Android, GuestSystem::OpenHarmony).level,
        SupportLevel::Refused
    );
    assert_eq!(
        cell(HostPlatform::Linux, GuestSystem::OpenHarmony).level,
        SupportLevel::Limited
    );
    assert_eq!(
        cell(HostPlatform::OpenHarmony, GuestSystem::OpenHarmony).level,
        SupportLevel::Ready
    );
    assert_eq!(
        cell(HostPlatform::OpenHarmony, GuestSystem::Ios).level,
        SupportLevel::Refused
    );
    assert!(cell(HostPlatform::Android, GuestSystem::OpenHarmony)
        .legal_note
        .contains("does not translate a HAP"));
    let exported = export_json();
    let share = fs::read_to_string(
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../share/compatibility.json"),
    )
    .unwrap();
    assert_eq!(share, exported);
    assert!(exported.contains("\"license\": \"Apache-2.0\""));
    assert!(exported.contains("\"host\": \"macos\""));
    assert!(!exported.contains("mac_os"));
    assert!(!exported.contains("dual-licensed"));
}

#[test]
fn runs_a_linux_script_and_keeps_arguments_intact() {
    let (root, layout) = scratch();
    let program = root.join("hello; echo pwned.sh");
    script(
        &program,
        "#!/bin/sh\nprintf 'olympium-guest-ok platform=linux format=script\\n'\nprintf 'args=%s\\n' \"$1\"\n",
    );
    let mut request = file_request(program.to_str().unwrap(), PackageKind::LinuxScript);
    request.args = vec!["hello world".into()];
    let prepared = prepare(
        HostPlatform::Linux,
        &request,
        &Config::default(),
        &SystemProbe,
    )
    .unwrap();
    let execution = execute(&prepared, &capture(), &layout).unwrap();
    assert!(execution
        .stdout
        .contains("olympium-guest-ok platform=linux format=script"));
    assert!(execution.stdout.contains("args=hello world"));
    assert!(!root.join("pwned").exists());
    let _ = fs::remove_dir_all(root);
}

#[test]
fn runs_a_deb_without_installing_it() {
    let (root, layout) = scratch();
    let deb = root.join("hello.deb");
    let bytes =
        sample_deb_bytes("#!/bin/sh\nprintf 'olympium-guest-ok platform=linux format=deb\\n'\n")
            .unwrap();
    fs::write(&deb, bytes).unwrap();
    let request = request_for_file(&deb, Some("Hello"), None, None, None, Vec::new()).unwrap();
    assert_eq!(request.kind, PackageKind::LinuxDeb);
    let prepared = prepare(
        HostPlatform::Linux,
        &request,
        &Config::default(),
        &SystemProbe,
    )
    .unwrap();
    let execution = execute(&prepared, &capture(), &layout).unwrap();
    assert!(execution
        .stdout
        .contains("olympium-guest-ok platform=linux format=deb"));
    assert_eq!(execution.engine_id, "deb");
    let _ = fs::remove_dir_all(root);
}

#[test]
fn runs_an_rpm_through_a_fake_extractor() {
    let (root, layout) = scratch();
    let cpio = root.join("payload.cpio");
    fs::write(
        &cpio,
        encode_newc(&[(
            "usr/bin/hello-olympium",
            b"#!/bin/sh\nprintf 'olympium-guest-ok platform=linux format=rpm\\n'\n",
            0o755,
        )]),
    )
    .unwrap();
    let extractor = root.join("rpm2cpio");
    script(
        &extractor,
        &format!("#!/bin/sh\ncat '{}'\n", cpio.display()),
    );
    let rpm = root.join("hello.rpm");
    fs::write(&rpm, [0xed, 0xab, 0xee, 0xdb]).unwrap();
    let request = request_for_file(&rpm, None, None, None, None, Vec::new()).unwrap();
    assert_eq!(request.kind, PackageKind::LinuxRpm);
    let probe = MapProbe::new().with("rpm2cpio", extractor.to_str().unwrap());
    let prepared = prepare(HostPlatform::Linux, &request, &Config::default(), &probe).unwrap();
    let execution = execute(&prepared, &capture(), &layout).unwrap();
    assert!(execution.stdout.contains("format=rpm"));
    let _ = fs::remove_dir_all(root);
}

#[test]
fn wine_receives_the_executable_as_one_argument() {
    let (root, layout) = scratch();
    let exe = root.join("hello; touch pwned.exe");
    fs::write(&exe, b"MZ fake").unwrap();
    let wine = root.join("wine64");
    script(
        &wine,
        "#!/bin/sh\nprintf '%s\\n' \"$1\"\nprintf 'olympium-guest-ok platform=windows\\n'\n",
    );
    let request = file_request(exe.to_str().unwrap(), PackageKind::WindowsExe);
    let probe = MapProbe::new().with("wine64", wine.to_str().unwrap());
    let prepared = prepare(HostPlatform::Linux, &request, &Config::default(), &probe).unwrap();
    assert_eq!(prepared.engine_id, "wine");
    let execution = execute(&prepared, &capture(), &layout).unwrap();
    assert!(execution
        .stdout
        .contains("olympium-guest-ok platform=windows"));
    assert!(execution.stdout.contains("hello; touch pwned.exe"));
    assert!(!root.join("pwned.exe").exists());
    let _ = fs::remove_dir_all(root);
}

#[test]
fn missing_wine_does_not_execute_the_program() {
    let (root, _) = scratch();
    let exe = root.join("hello.exe");
    fs::write(&exe, b"MZ").unwrap();
    let request = file_request(exe.to_str().unwrap(), PackageKind::WindowsExe);
    let err = prepare(
        HostPlatform::Linux,
        &request,
        &Config::default(),
        &MapProbe::new(),
    )
    .unwrap_err();
    assert_eq!(err.kind(), "engine_missing");
    assert!(err.message().contains("Windows license"));
    let _ = fs::remove_dir_all(root);
}

#[test]
fn refuses_macos_and_ios_files_on_linux_without_a_command() {
    let (root, layout) = scratch();
    let dmg = root.join("App.dmg");
    fs::write(&dmg, b"dmg").unwrap();
    let request = request_for_file(&dmg, None, None, None, None, Vec::new()).unwrap();
    let err = prepare(
        HostPlatform::Linux,
        &request,
        &Config::default(),
        &SystemProbe,
    )
    .unwrap_err();
    assert_eq!(err.id(), Some("apple-platform-on-non-apple-hardware"));
    let body = err.to_json().to_string();
    for forbidden in ["hdiutil", "simctl", "xcrun", "qemu", "darling", "ipsw"] {
        assert!(!body.contains(forbidden), "{forbidden} leaked into {body}");
    }
    let macho = root.join("program");
    fs::write(&macho, [0xfe, 0xed, 0xfa, 0xcf, 0, 0, 0, 0]).unwrap();
    let relabel = request_for_file(
        &macho,
        None,
        Some(PackageKind::LinuxExecutable),
        Some(GuestSystem::Linux),
        None,
        Vec::new(),
    );
    assert!(relabel.is_err());
    let mac_plan = prepare(
        HostPlatform::MacOs,
        &file_request("/tmp/Example.dmg", PackageKind::MacOsDmg),
        &Config::default(),
        &SystemProbe,
    )
    .unwrap();
    let executed = execute(&mac_plan, &capture(), &layout).unwrap_err();
    assert_eq!(executed.kind(), "legal");
    let ipa = prepare(
        HostPlatform::MacOs,
        &file_request("/tmp/App.ipa", PackageKind::IosPackage),
        &Config::default(),
        &SystemProbe,
    )
    .unwrap_err();
    assert_eq!(ipa.id(), Some("ios-package-not-installed"));
    let _ = fs::remove_dir_all(root);
}

#[test]
fn wsl_plan_translates_windows_paths() {
    let request = file_request(r"C:\Apps\hello.sh", PackageKind::LinuxScript);
    let probe = MapProbe::new().with("wsl.exe", "wsl.exe");
    let prepared = prepare(HostPlatform::Windows, &request, &Config::default(), &probe).unwrap();
    let LaunchKind::Process { program, args, .. } = prepared.launch else {
        panic!("expected a process plan");
    };
    assert_eq!(program, "wsl.exe");
    assert!(args.windows(2).any(|pair| pair == ["--exec", "/bin/sh"]));
    assert!(args.iter().any(|arg| arg == "/mnt/c/Apps/hello.sh"));
}

#[test]
fn android_runtime_prefers_waydroid_and_can_use_adb() {
    let (root, _) = scratch();
    let apk = root.join("app.apk");
    fs::write(&apk, b"PK\x03\x04").unwrap();
    fs::write(root.join("app.apk"), b"PK\x03\x04AndroidManifest.xml").unwrap();
    let request = request_for_file(
        &apk,
        None,
        None,
        None,
        Some("com.example.app".into()),
        Vec::new(),
    )
    .unwrap();
    let both = MapProbe::new()
        .with("waydroid", "/usr/bin/waydroid")
        .with("adb", "/usr/bin/adb");
    let prepared = prepare(HostPlatform::Linux, &request, &Config::default(), &both).unwrap();
    let LaunchKind::Sequence { steps } = prepared.launch else {
        panic!("expected steps");
    };
    assert_eq!(steps[0].program, "/usr/bin/waydroid");
    let mut config = Config::default();
    config.engines.android.prefer = "adb".into();
    let prepared = prepare(HostPlatform::Linux, &request, &config, &both).unwrap();
    let LaunchKind::Sequence { steps } = prepared.launch else {
        panic!("expected steps");
    };
    assert_eq!(steps[0].args, vec!["install", "-r", apk.to_str().unwrap()]);
    assert_eq!(steps[1].program, "/usr/bin/adb");
    let _ = fs::remove_dir_all(root);
}

#[test]
fn android_hook_runs_a_configured_command() {
    let (root, layout) = scratch();
    let program = root.join("hello.sh");
    script(
        &program,
        "#!/bin/sh\nprintf 'olympium-guest-ok platform=linux format=hook\\n'\n",
    );
    let runner = root.join("runner");
    script(&runner, "#!/bin/sh\nexec \"$1\"\n");
    let mut config = Config::default();
    config.engines.linux_hook.prefix = vec![runner.to_str().unwrap().to_string()];
    let request = file_request(program.to_str().unwrap(), PackageKind::LinuxExecutable);
    let prepared = prepare(HostPlatform::Android, &request, &config, &MapProbe::new()).unwrap();
    let execution = execute(&prepared, &capture(), &layout).unwrap();
    assert!(execution.stdout.contains("format=hook"));
    let _ = fs::remove_dir_all(root);
}

#[test]
fn flatpak_and_snap_reject_urls_and_run_installed_names() {
    let (root, layout) = scratch();
    assert!(request_for_flatpak("https://example.invalid/app", None, Vec::new()).is_err());
    let flatpak = root.join("flatpak");
    script(
        &flatpak,
        "#!/bin/sh\nprintf '%s\\n' \"$1\" \"$2\"\nprintf 'olympium-guest-ok platform=linux format=flatpak\\n'\n",
    );
    let request = request_for_flatpak("org.example.App", None, Vec::new()).unwrap();
    let probe = MapProbe::new().with("flatpak", flatpak.to_str().unwrap());
    let prepared = prepare(HostPlatform::Linux, &request, &Config::default(), &probe).unwrap();
    let execution = execute(&prepared, &capture(), &layout).unwrap();
    assert!(execution.stdout.contains("run\norg.example.App"));
    let snap = root.join("snap");
    script(
        &snap,
        "#!/bin/sh\nprintf 'olympium-guest-ok platform=linux format=snap\\n'\n",
    );
    let request = request_for_snap("hello-olympium", None, Vec::new()).unwrap();
    let probe = MapProbe::new().with("snap", snap.to_str().unwrap());
    let prepared = prepare(HostPlatform::Linux, &request, &Config::default(), &probe).unwrap();
    execute(&prepared, &capture(), &layout).unwrap();
    let _ = fs::remove_dir_all(root);
}

#[test]
fn windows_plan_does_not_run_on_linux() {
    let (_, layout) = scratch();
    let prepared = prepare(
        HostPlatform::Windows,
        &file_request(r"C:\Apps\hello.exe", PackageKind::WindowsExe),
        &Config::default(),
        &SystemProbe,
    )
    .unwrap();
    let err = execute(&prepared, &capture(), &layout).unwrap_err();
    assert_eq!(err.kind(), "wrong_host");
}

#[test]
fn library_config_and_sessions_roundtrip() {
    let (root, layout) = scratch();
    let mut config = Config::default();
    config.appearance = Appearance::Dark;
    save_config(&layout.config, &config).unwrap();
    assert_eq!(
        load_config(&layout.config).unwrap().appearance,
        Appearance::Dark
    );
    let mut catalog = Catalog::default();
    let program = catalog.add(file_request("/tmp/hello.sh", PackageKind::LinuxScript));
    save_catalog(&layout.catalog, &catalog).unwrap();
    let loaded = load_catalog(&layout.catalog).unwrap();
    assert_eq!(loaded.get(&program.id).unwrap().name, "sample");
    append_session(
        &layout.sessions,
        &SessionRecord {
            at_unix_ms: 1,
            name: "sample".into(),
            guest: GuestSystem::Linux,
            engine_id: "native".into(),
            ok: true,
            message: "ran".into(),
        },
    )
    .unwrap();
    assert_eq!(read_sessions(&layout.sessions, 10).unwrap().len(), 1);
    let _ = fs::remove_dir_all(root);
}

#[test]
fn java_class_is_not_a_macho_and_a_fat_binary_is() {
    let mut class = vec![0xca, 0xfe, 0xba, 0xbe, 0, 0, 0, 61];
    class.extend(std::iter::repeat(0).take(8));
    assert!(!is_macho(&class));
    let mut fat = vec![0xca, 0xfe, 0xba, 0xbe, 0, 0, 0, 1];
    fat.extend_from_slice(&0x0100_0007u32.to_be_bytes());
    assert!(is_macho(&fat));
}

#[test]
fn source_has_no_apple_guest_bypass() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
    let needles = [
        "darling",
        "ipsw",
        "docker-osx",
        "osx-kvm",
        "corellium",
        "checkra1n",
        "palera1n",
        "sideloadly",
    ];
    let mut stack = vec![root.join("crates")];
    while let Some(dir) = stack.pop() {
        for entry in fs::read_dir(&dir).unwrap() {
            let entry = entry.unwrap();
            let path = entry.path();
            if path.is_dir() {
                stack.push(path);
                continue;
            }
            if path.extension().and_then(|ext| ext.to_str()) != Some("rs") {
                continue;
            }
            if !path.to_string_lossy().contains("/src/") {
                continue;
            }
            let text = fs::read_to_string(&path).unwrap().to_ascii_lowercase();
            for needle in needles {
                assert!(
                    !text.contains(needle),
                    "{} contains {needle}",
                    path.display()
                );
            }
        }
    }
}

#[test]
fn host_copies_match_the_share_file() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = fs::read_to_string(root.join("share/compatibility.json")).unwrap();
    for relative in [
        "hosts/windows/Olympium.Windows/Assets/compatibility.json",
        "hosts/android/app/src/main/assets/compatibility.json",
        "hosts/macos/Olympium/Resources/compatibility.json",
    ] {
        let copy = fs::read_to_string(root.join(relative)).unwrap();
        assert_eq!(copy, source, "{relative} is out of date");
    }
}

#[test]
fn license_files_are_apache_2() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
    let license = fs::read_to_string(root.join("LICENSE")).unwrap();
    assert!(license.contains("Apache License"));
    assert!(license.contains("Version 2.0, January 2004"));
    let notice = fs::read_to_string(root.join("NOTICE")).unwrap();
    assert!(notice.contains("Apache-2.0"));
    assert!(notice.contains("Ye Zicheng"));
    assert!(notice.contains("not dual-licensed"));
}
