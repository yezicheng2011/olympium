// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use linkspan_core::*;
use std::collections::BTreeMap;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

fn scratch() -> (PathBuf, Layout) {
    static N: AtomicU64 = AtomicU64::new(0);
    let path = std::env::temp_dir().join(format!(
        "linkspan-oh-{}-{}",
        std::process::id(),
        N.fetch_add(1, Ordering::Relaxed)
    ));
    fs::create_dir_all(&path).unwrap();
    (path.clone(), Layout::under(&path))
}

fn stored_zip(files: &[(&str, &[u8])]) -> Vec<u8> {
    fn crc32(data: &[u8]) -> u32 {
        let mut crc = 0xffff_ffffu32;
        for byte in data {
            crc ^= *byte as u32;
            for _ in 0..8 {
                let mask = (crc & 1).wrapping_neg();
                crc = (crc >> 1) ^ (0xedb8_8320 & mask);
            }
        }
        !crc
    }
    let mut locals = Vec::new();
    let mut central = Vec::new();
    let mut offset = 0u32;
    for (name, data) in files {
        let name_bytes = name.as_bytes();
        let crc = crc32(data).to_le_bytes();
        let size = (data.len() as u32).to_le_bytes();
        let name_len = (name_bytes.len() as u16).to_le_bytes();
        let mut local = Vec::new();
        local.extend_from_slice(&0x04034b50u32.to_le_bytes());
        local.extend_from_slice(&20u16.to_le_bytes());
        local.extend_from_slice(&0u16.to_le_bytes());
        local.extend_from_slice(&0u16.to_le_bytes());
        local.extend_from_slice(&0u16.to_le_bytes());
        local.extend_from_slice(&0u16.to_le_bytes());
        local.extend_from_slice(&crc);
        local.extend_from_slice(&size);
        local.extend_from_slice(&size);
        local.extend_from_slice(&name_len);
        local.extend_from_slice(&0u16.to_le_bytes());
        local.extend_from_slice(name_bytes);
        let mut central_header = Vec::new();
        central_header.extend_from_slice(&0x02014b50u32.to_le_bytes());
        central_header.extend_from_slice(&20u16.to_le_bytes());
        central_header.extend_from_slice(&20u16.to_le_bytes());
        central_header.extend_from_slice(&0u16.to_le_bytes());
        central_header.extend_from_slice(&0u16.to_le_bytes());
        central_header.extend_from_slice(&0u16.to_le_bytes());
        central_header.extend_from_slice(&0u16.to_le_bytes());
        central_header.extend_from_slice(&crc);
        central_header.extend_from_slice(&size);
        central_header.extend_from_slice(&size);
        central_header.extend_from_slice(&name_len);
        central_header.extend_from_slice(&0u16.to_le_bytes());
        central_header.extend_from_slice(&0u16.to_le_bytes());
        central_header.extend_from_slice(&0u16.to_le_bytes());
        central_header.extend_from_slice(&0u16.to_le_bytes());
        central_header.extend_from_slice(&0u32.to_le_bytes());
        central_header.extend_from_slice(&offset.to_le_bytes());
        central_header.extend_from_slice(name_bytes);
        offset += local.len() as u32 + data.len() as u32;
        locals.extend_from_slice(&local);
        locals.extend_from_slice(data);
        central.extend_from_slice(&central_header);
    }
    let cd_size = central.len() as u32;
    let cd_offset = locals.len() as u32;
    let mut out = locals;
    out.extend_from_slice(&central);
    out.extend_from_slice(&0x06054b50u32.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&(files.len() as u16).to_le_bytes());
    out.extend_from_slice(&(files.len() as u16).to_le_bytes());
    out.extend_from_slice(&cd_size.to_le_bytes());
    out.extend_from_slice(&cd_offset.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out
}

fn write_hap(path: &Path, module: &str, signed: bool) {
    let mut files: Vec<(&str, &[u8])> = vec![
        ("module.json", module.as_bytes()),
        ("ets/modules.abc", b"abc"),
    ];
    let sf = b"Signature-Version: 1.0\n";
    let rsa = b"not-a-real-certificate";
    if signed {
        files.push(("META-INF/CERT.SF", sf));
        files.push(("META-INF/CERT.RSA", rsa));
    }
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).unwrap();
    }
    fs::write(path, stored_zip(&files)).unwrap();
}

fn touch_exec(path: &Path) {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).unwrap();
    }
    fs::write(path, b"#!/bin/sh\nexit 0\n").unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(path, fs::Permissions::from_mode(0o755)).unwrap();
    }
}

fn facts(root: &Path, identity: OhIdentity, developer: Option<bool>, device: bool) -> OhFacts {
    let hdc = root.join("hdc");
    let runtime = root.join("linkspan-runtime");
    let bridge = root.join("linkspan-surface");
    touch_exec(&hdc);
    touch_exec(&runtime);
    touch_exec(&bridge);
    OhFacts {
        identity,
        developer_mode: developer,
        device_connected: device,
        hdc: Some(hdc.to_string_lossy().into_owned()),
        runtime: Some(runtime.to_string_lossy().into_owned()),
        bridge: Some(bridge.to_string_lossy().into_owned()),
        image: Some(root.to_string_lossy().into_owned()),
    }
}

fn fixed(facts: OhFacts) -> FixedOpenHarmony {
    FixedOpenHarmony { facts }
}

fn launch_text(prepared: &Prepared) -> String {
    match &prepared.launch {
        LaunchKind::Process { program, args, .. } => format!("{program} {}", args.join(" ")),
        LaunchKind::Sequence { steps } => steps
            .iter()
            .map(|step| format!("{} {}", step.program, step.args.join(" ")))
            .collect::<Vec<_>>()
            .join("\n"),
        other => format!("{other:?}"),
    }
}

#[test]
fn classifies_hap_and_app_and_keeps_apk_and_mac_app() {
    let (root, _) = scratch();
    let hap = root.join("entry.hap");
    write_hap(
        &hap,
        r#"{"bundleName":"com.example.hello","mainElement":"EntryAbility"}"#,
        false,
    );
    let class = classify_path(&hap).unwrap();
    assert_eq!(class.kind, PackageKind::OpenHarmonyHap);
    assert_eq!(class.guest, GuestSystem::OpenHarmony);

    let apk = root.join("app.apk");
    fs::write(&apk, stored_zip(&[("AndroidManifest.xml", b"<manifest/>")])).unwrap();
    assert_eq!(classify_path(&apk).unwrap().kind, PackageKind::AndroidApk);

    let both = root.join("both.hap");
    fs::write(
        &both,
        stored_zip(&[
            ("AndroidManifest.xml", b"<manifest/>"),
            ("module.json", b"{}"),
        ]),
    )
    .unwrap();
    assert!(classify_path(&both).is_err());

    let app = root.join("entry.app");
    let inner = stored_zip(&[("module.json", br#"{"bundleName":"com.example.pack"}"#)]);
    fs::write(
        &app,
        stored_zip(&[
            (
                "pack.info",
                br#"{"summary":{"app":{"bundleName":"com.example.pack"}}}"#,
            ),
            ("entry-default.hap", inner.as_slice()),
        ]),
    )
    .unwrap();
    assert_eq!(
        classify_path(&app).unwrap().kind,
        PackageKind::OpenHarmonyApp
    );

    let apk_with_asset = root.join("with-asset.apk");
    fs::write(
        &apk_with_asset,
        stored_zip(&[
            ("AndroidManifest.xml", b"<manifest/>"),
            ("assets/config.json", br#"{"name":"not-harmony"}"#),
        ]),
    )
    .unwrap();
    assert_eq!(
        classify_path(&apk_with_asset).unwrap().kind,
        PackageKind::AndroidApk
    );
    let ipa = root.join("App.ipa");
    fs::write(
        &ipa,
        stored_zip(&[
            ("Payload/App.app/Info.plist", b"<plist/>"),
            ("assets/config.json", br#"{"name":"not-harmony"}"#),
        ]),
    )
    .unwrap();
    assert_eq!(classify_path(&ipa).unwrap().kind, PackageKind::IosPackage);

    let mac = root.join("Example.app");
    fs::create_dir_all(mac.join("Contents")).unwrap();
    fs::write(mac.join("Contents/Info.plist"), "<plist></plist>").unwrap();
    assert_eq!(classify_path(&mac).unwrap().kind, PackageKind::MacOsApp);
    let _ = fs::remove_dir_all(root);
}

#[test]
fn android_refuses_a_hap_without_building_a_translation() {
    let (root, _) = scratch();
    let hap = root.join("entry.hap");
    write_hap(&hap, r#"{"bundleName":"com.example.hello"}"#, true);
    let request = request_for_file(&hap, None, None, None, None, Vec::new()).unwrap();
    let err = prepare(
        HostPlatform::Android,
        &request,
        &Config::default(),
        &MapProbe::new(),
    )
    .unwrap_err();
    assert_eq!(err.id(), Some("openharmony-on-android"));
    let body = err.message().to_ascii_lowercase();
    for forbidden in ["wine", "adb", "apk", "waydroid"] {
        assert!(!body.contains(forbidden), "{forbidden} in {body}");
    }
    assert!(body.contains("does not translate"));
    let _ = fs::remove_dir_all(root);
}

#[test]
fn unsigned_hap_needs_developer_mode_and_signed_hap_does_not() {
    let (root, _) = scratch();
    let hap = root.join("entry.hap");
    write_hap(&hap, r#"{"bundleName":"com.example.hello"}"#, false);
    let request = request_for_file(
        &hap,
        None,
        None,
        None,
        Some("com.example.hello".into()),
        Vec::new(),
    )
    .unwrap();
    let locked = facts(&root, OhIdentity::OpenHarmony, Some(false), true);
    let err = prepare_with(
        HostPlatform::Linux,
        &request,
        &Config::default(),
        &MapProbe::new(),
        &fixed(locked),
    )
    .unwrap_err();
    assert_eq!(err.id(), Some("openharmony-unsigned"));

    let open = facts(&root, OhIdentity::OpenHarmony, Some(true), true);
    let prepared = prepare_with(
        HostPlatform::Linux,
        &request,
        &Config::default(),
        &MapProbe::new(),
        &fixed(open),
    )
    .unwrap();
    let text = launch_text(&prepared);
    assert!(text.contains("hdc"));
    assert!(text.contains("install"));
    assert!(!text.to_ascii_lowercase().contains("wine"));
    assert!(!text.contains("adb"));
    assert!(!text.contains(".apk"));
    assert_eq!(prepared.surface.hides_runtime_chrome, true);
    assert_eq!(prepared.surface.mode, "device");

    let signed = root.join("signed.hap");
    write_hap(&signed, r#"{"bundleName":"com.example.signed"}"#, true);
    let request = request_for_file(&signed, None, None, None, None, Vec::new()).unwrap();
    let no_dev = facts(&root, OhIdentity::OpenHarmony, Some(false), true);
    let prepared = prepare_with(
        HostPlatform::Linux,
        &request,
        &Config::default(),
        &MapProbe::new(),
        &fixed(no_dev),
    )
    .unwrap();
    assert!(launch_text(&prepared).contains("install"));
    let _ = fs::remove_dir_all(root);
}

#[test]
fn commercial_harmonyos_and_local_image_bridge_are_gated() {
    let (root, _) = scratch();
    let hap = root.join("entry.hap");
    write_hap(&hap, r#"{"bundleName":"com.example.hello"}"#, true);
    let request = request_for_file(&hap, None, None, None, None, Vec::new()).unwrap();
    let commercial = facts(&root, OhIdentity::CommercialHarmony, Some(true), true);
    let err = prepare_with(
        HostPlatform::MacOs,
        &request,
        &Config::default(),
        &MapProbe::new(),
        &fixed(commercial),
    )
    .unwrap_err();
    assert_eq!(err.id(), Some("commercial-harmonyos"));

    let mut local = facts(&root, OhIdentity::OpenHarmony, Some(true), false);
    local.bridge = None;
    let err = prepare_with(
        HostPlatform::Windows,
        &request,
        &Config::default(),
        &MapProbe::new(),
        &fixed(local),
    )
    .unwrap_err();
    assert!(err.message().contains("linkspan-surface"));
    assert!(err.message().contains("nested emulator"));

    let mut config = Config::default();
    config.engines.experience.shared_folders = vec!["/home/me/Documents".into()];
    config.appearance = Appearance::Dark;
    let local = facts(&root, OhIdentity::OpenHarmony, Some(true), false);
    let prepared = prepare_with(
        HostPlatform::Linux,
        &request,
        &config,
        &MapProbe::new(),
        &fixed(local),
    )
    .unwrap();
    let text = launch_text(&prepared);
    assert!(text.contains("linkspan-runtime start"));
    assert!(text.contains("--display host-window"));
    assert!(text.contains("--appearance dark"));
    assert!(text.contains("--share /home/me/Documents"));
    assert!(text.contains("linkspan-surface"));
    assert!(!text.contains("qemu"));
    assert_eq!(prepared.surface.mode, "host_window");
    let _ = fs::remove_dir_all(root);
}

#[test]
fn openharmony_host_runs_only_openharmony_packages() {
    let (root, _) = scratch();
    let hap = root.join("entry.hap");
    write_hap(
        &hap,
        r#"{"bundleName":"com.example.hello","mainElement":"MainAbility"}"#,
        false,
    );
    let request = request_for_file(&hap, None, None, None, None, Vec::new()).unwrap();
    let dev = facts(&root, OhIdentity::OpenHarmony, Some(true), false);
    let prepared = prepare_with(
        HostPlatform::OpenHarmony,
        &request,
        &Config::default(),
        &MapProbe::new(),
        &fixed(dev),
    )
    .unwrap();
    assert_eq!(prepared.engine_id, "openharmony-native");
    assert_eq!(prepared.required_host, Some(HostPlatform::OpenHarmony));
    let text = launch_text(&prepared);
    assert!(text.contains("bm install"));
    assert!(text.contains("aa start"));
    assert!(text.contains("com.example.hello"));
    assert!(text.contains("MainAbility"));
    assert!(!text.contains("hdc"));

    let exe = root.join("hello.exe");
    fs::write(&exe, b"MZ").unwrap();
    let windows = file_request(exe.to_str().unwrap(), PackageKind::WindowsExe);
    let err = prepare_with(
        HostPlatform::OpenHarmony,
        &windows,
        &Config::default(),
        &MapProbe::new(),
        &fixed(facts(&root, OhIdentity::OpenHarmony, Some(true), false)),
    )
    .unwrap_err();
    assert_eq!(err.id(), Some("openharmony-host-native-only"));
    let _ = fs::remove_dir_all(root);
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

struct MapNet {
    text: BTreeMap<String, String>,
    probe: BTreeMap<String, bool>,
    files: BTreeMap<String, Vec<u8>>,
}

impl Net for MapNet {
    fn get_text(&self, url: &str) -> Result<String, linkspan_core::Error> {
        self.text
            .get(url)
            .cloned()
            .ok_or_else(|| linkspan_core::Error::Io {
                message: format!("no text for {url}"),
            })
    }

    fn probe(&self, url: &str) -> bool {
        self.probe.get(url).copied().unwrap_or(false)
    }

    fn download(
        &self,
        url: &str,
        dest: &Path,
        progress: &mut dyn FnMut(Progress),
    ) -> Result<(), linkspan_core::Error> {
        let bytes = self
            .files
            .get(url)
            .ok_or_else(|| linkspan_core::Error::Io {
                message: format!("no file for {url}"),
            })?;
        progress(Progress {
            received: 0,
            total: Some(bytes.len() as u64),
        });
        if let Some(parent) = dest.parent() {
            fs::create_dir_all(parent).unwrap();
        }
        let mut file = fs::File::create(dest).unwrap();
        let mid = bytes.len() / 2;
        file.write_all(&bytes[..mid]).unwrap();
        progress(Progress {
            received: mid as u64,
            total: Some(bytes.len() as u64),
        });
        file.write_all(&bytes[mid..]).unwrap();
        progress(Progress {
            received: bytes.len() as u64,
            total: Some(bytes.len() as u64),
        });
        Ok(())
    }
}

#[test]
fn mirror_follows_client_ip_and_falls_back_like_a_probe() {
    let mirrors = Config::default().engines.mirrors;
    let mut net = MapNet {
        text: BTreeMap::new(),
        probe: BTreeMap::new(),
        files: BTreeMap::new(),
    };
    net.text.insert(
        mirrors.ip_lookup_url.clone(),
        r#"{"ip":"1.2.3.4","country":"CN"}"#.into(),
    );
    let choice = choose_mirror(&mirrors, &net).unwrap();
    assert_eq!(choice.source, MirrorSource::China);
    assert_eq!(choice.client_ip.as_deref(), Some("1.2.3.4"));

    net.text.insert(
        mirrors.ip_lookup_url.clone(),
        r#"{"ip":"8.8.8.8","country":"US"}"#.into(),
    );
    let choice = choose_mirror(&mirrors, &net).unwrap();
    assert_eq!(choice.source, MirrorSource::Github);
    assert_eq!(choice.country.as_deref(), Some("US"));

    net.text.clear();
    net.probe.insert(mirrors.probe_url.clone(), false);
    let choice = choose_mirror(&mirrors, &net).unwrap();
    assert_eq!(choice.source, MirrorSource::China);
    assert!(choice.reason.contains("GitHub did not answer"));

    let mut forced = mirrors.clone();
    forced.prefer = "github".into();
    net.text.insert(
        forced.ip_lookup_url.clone(),
        r#"{"ip":"1.2.3.4","country":"CN"}"#.into(),
    );
    let choice = choose_mirror(&forced, &net).unwrap();
    assert_eq!(choice.source, MirrorSource::Github);
}

#[test]
fn unpublished_slim_image_is_not_invented_and_a_published_one_is_checked() {
    let (root, layout) = scratch();
    let mut lock = bundled_lock();
    for entry in &lock.runtimes {
        match entry.id.as_str() {
            "android" | "openharmony" => {
                assert!(!entry.published, "{}", entry.id);
                assert!(entry.sha256.is_empty(), "{}", entry.id);
            }
            "linux" => {
                assert!(entry.published);
                for arch in ["x86_64", "aarch64"] {
                    let sha = entry.sha256.get(arch).expect(arch);
                    assert_eq!(sha.len(), 64);
                    assert!(sha.chars().all(|c| c.is_ascii_hexdigit()));
                }
            }
            "wine" => {
                assert!(entry.published);
                let sha = entry.sha256.get("x86_64").unwrap();
                assert_eq!(sha.len(), 64);
                assert!(entry.assets.aarch64.is_empty());
                assert!(entry.sha256.get("aarch64").is_none());
            }
            other => panic!("unexpected runtime {other}"),
        }
    }
    assert!(lock
        .runtimes
        .iter()
        .any(|entry| entry.repo == "https://github.com/yezicheng2011/olympium-openharmony"));
    let mirrors = Config::default().engines.mirrors;
    let mut net = MapNet {
        text: BTreeMap::new(),
        probe: BTreeMap::new(),
        files: BTreeMap::new(),
    };
    net.text.insert(
        mirrors.ip_lookup_url.clone(),
        r#"{"ip":"203.0.113.8","country":"CN"}"#.into(),
    );
    let report = fetch_runtime(
        "openharmony",
        &lock,
        &mirrors,
        &net,
        &layout,
        "x86_64",
        |_| {},
    )
    .unwrap();
    assert_eq!(report.outcome, "not_published");
    assert_eq!(report.source, "china");
    assert!(!layout.runtimes.join("openharmony").exists());

    let payload = {
        let mut builder = tar::Builder::new(Vec::new());
        let mut header = tar::Header::new_gnu();
        let body = b"openharmony-slim 0.1.0 developer-mode\n";
        header.set_size(body.len() as u64);
        header.set_mode(0o644);
        header.set_cksum();
        builder
            .append_data(&mut header, "EDITION", &body[..])
            .unwrap();
        builder.into_inner().unwrap()
    };
    let mut encoded = Vec::new();
    {
        let mut encoder =
            flate2::write::GzEncoder::new(&mut encoded, flate2::Compression::default());
        encoder.write_all(&payload).unwrap();
        encoder.finish().unwrap();
    }
    use sha2::{Digest, Sha256};
    let digest = hex_of(&Sha256::digest(&encoded));
    let entry = lock
        .runtimes
        .iter_mut()
        .find(|entry| entry.id == "openharmony")
        .unwrap();
    entry.published = true;
    entry.sha256.insert("x86_64".into(), digest);
    let asset = entry.assets.x86_64.clone();
    let url = format!(
        "https://gh-proxy.com/https://github.com/yezicheng2011/olympium-openharmony/releases/download/v0.1.0/{asset}"
    );
    net.files.insert(url.clone(), encoded);
    let mut seen = Vec::new();
    let report = fetch_runtime(
        "openharmony",
        &lock,
        &mirrors,
        &net,
        &layout,
        "x86_64",
        |progress| seen.push(progress.received),
    )
    .unwrap();
    assert_eq!(report.outcome, "downloaded");
    assert!(seen.windows(2).any(|pair| pair[1] >= pair[0]));
    assert!(seen.last().copied().unwrap_or(0) > 0);
    let edition = fs::read_to_string(layout.runtimes.join("openharmony").join("EDITION")).unwrap();
    assert!(edition.contains("openharmony-slim"));
    assert!(edition.contains("developer-mode"));
    let _ = fs::remove_dir_all(root);
}

fn hex_of(bytes: &[u8]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut out = String::new();
    for byte in bytes {
        out.push(HEX[(byte >> 4) as usize] as char);
        out.push(HEX[(byte & 0xf) as usize] as char);
    }
    out
}

#[test]
fn memory_budget_stays_inside_the_configured_range() {
    let mut experience = Config::default().engines.experience;
    assert_eq!(memory_budget_mib(&experience, None), 2048);
    assert_eq!(memory_budget_mib(&experience, Some(32768)), 7680);
    experience.memory_mode = "manual".into();
    experience.memory_manual_mib = 3000;
    assert_eq!(memory_budget_mib(&experience, Some(1024)), 3000);
}

#[test]
fn desktop_launch_uses_the_manifest_bundle_and_ability() {
    let (root, _) = scratch();
    let hap = root.join("entry.hap");
    write_hap(
        &hap,
        r#"{"bundleName":"com.example.hello","mainElement":"MainAbility"}"#,
        true,
    );
    let request = request_for_file(&hap, None, None, None, None, Vec::new()).unwrap();
    assert!(request.selector.is_none());
    let prepared = prepare_with(
        HostPlatform::Linux,
        &request,
        &Config::default(),
        &MapProbe::new(),
        &fixed(facts(&root, OhIdentity::OpenHarmony, Some(true), true)),
    )
    .unwrap();
    let text = launch_text(&prepared);
    assert!(text.contains("com.example.hello"));
    assert!(text.contains("MainAbility"));
    assert!(!text.contains("EntryAbility"));
    let _ = fs::remove_dir_all(root);
}

#[test]
fn native_host_reads_local_developer_mode() {
    let (root, _) = scratch();
    let param = root.join("param");
    fs::write(
        &param,
        "#!/bin/sh\nif [ \"$2\" = const.security.developermode.state ]; then echo true; fi\nif [ \"$2\" = const.product.os.dist.name ]; then echo OpenHarmony; fi\n",
    )
    .unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(&param, fs::Permissions::from_mode(0o755)).unwrap();
    }
    let probe = MapProbe::new().with("param", param.to_str().unwrap());
    let on_device = SystemOpenHarmony.facts(HostPlatform::OpenHarmony, &Config::default(), &probe);
    assert_eq!(on_device.developer_mode, Some(true));
    assert_eq!(on_device.identity, OhIdentity::OpenHarmony);
    assert!(!on_device.device_connected);
    let on_linux = SystemOpenHarmony.facts(HostPlatform::Linux, &Config::default(), &probe);
    assert_eq!(on_linux.developer_mode, None);
    assert_eq!(on_linux.identity, OhIdentity::Unknown);
    let _ = fs::remove_dir_all(root);
}

#[test]
fn harmonyos_guest_name_is_not_accepted_as_commercial() {
    assert!(GuestSystem::parse("openharmony").is_some());
    assert!(GuestSystem::parse("harmonyos").is_none());
    assert!(GuestSystem::is_commercial_harmony_name("HarmonyOS"));
    assert_eq!(
        GuestSystem::parse("harmony"),
        Some(GuestSystem::OpenHarmony)
    );
}
