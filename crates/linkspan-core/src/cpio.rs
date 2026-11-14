// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use crate::error::Error;
use crate::util::{normalize_rel, safe_join};
use std::fs::{self, File};
use std::io::{self, Read};
use std::path::Path;
use std::process::{Command, Stdio};

const MAX_BYTES: u64 = 64 * 1024 * 1024;

/// Write a newc cpio archive. The RPM extractor reads this format from `rpm2cpio`.
pub fn encode_newc(files: &[(&str, &[u8], u32)]) -> Vec<u8> {
    let mut out = Vec::new();
    for (name, data, mode) in files {
        write_entry(&mut out, name, 0o100000 | (*mode & 0o777), data);
    }
    write_entry(&mut out, "TRAILER!!!", 0, b"");
    out
}

fn write_entry(out: &mut Vec<u8>, name: &str, mode: u32, data: &[u8]) {
    let mut name_bytes = name.as_bytes().to_vec();
    name_bytes.push(0);
    let namesize = name_bytes.len();
    let name_pad = (4 - ((110 + namesize) % 4)) % 4;
    let data_pad = (4 - (data.len() % 4)) % 4;
    let mut header = String::from("070701");
    let fields = [
        0u64,
        u64::from(mode),
        0,
        0,
        1,
        0,
        data.len() as u64,
        0,
        0,
        0,
        0,
        namesize as u64,
        0,
    ];
    for field in fields {
        header.push_str(&format!("{field:08x}"));
    }
    debug_assert_eq!(header.len(), 110);
    out.extend_from_slice(header.as_bytes());
    out.extend_from_slice(&name_bytes);
    out.extend(std::iter::repeat(0u8).take(name_pad));
    out.extend_from_slice(data);
    out.extend(std::iter::repeat(0u8).take(data_pad));
}

pub fn extract_rpm(archive: &Path, dest: &Path, rpm2cpio: &Path) -> Result<(), Error> {
    prepare_dest(dest)?;
    let mut child = Command::new(rpm2cpio)
        .arg(archive)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|err| Error::io(err, rpm2cpio.display()))?;
    let stdout = child.stdout.take().ok_or_else(|| Error::Io {
        message: "rpm2cpio produced no output".into(),
    })?;
    let extracted = extract_newc(stdout, dest);
    let output = child
        .wait_with_output()
        .map_err(|err| Error::io(err, "rpm2cpio"))?;
    extracted?;
    if !output.status.success() {
        return Err(Error::Failed {
            message: "rpm2cpio failed.".into(),
            guest_exit_code: output.status.code(),
            stdout: String::new(),
            stderr: String::from_utf8_lossy(&output.stderr).into_owned(),
        });
    }
    Ok(())
}

pub fn extract_newc<R: Read>(mut reader: R, dest: &Path) -> Result<(), Error> {
    prepare_dest(dest)?;
    let mut bytes = Vec::new();
    reader
        .by_ref()
        .take(MAX_BYTES + 1)
        .read_to_end(&mut bytes)
        .map_err(|err| Error::io(err, "cpio"))?;
    if bytes.len() as u64 > MAX_BYTES {
        return Err(Error::Target {
            message: "The RPM payload is larger than Linkspan will extract in memory (64 MB)."
                .into(),
        });
    }
    parse_newc(&bytes, dest)
}

fn prepare_dest(dest: &Path) -> Result<(), Error> {
    if dest.as_os_str().is_empty() || dest == Path::new("/") || dest.components().count() < 2 {
        return Err(Error::Target {
            message: "Refusing to extract into an unsafe directory.".into(),
        });
    }
    if dest.exists() {
        fs::remove_dir_all(dest).map_err(|err| Error::io(err, dest.display()))?;
    }
    fs::create_dir_all(dest).map_err(|err| Error::io(err, dest.display()))?;
    Ok(())
}

fn parse_newc(data: &[u8], dest: &Path) -> Result<(), Error> {
    let mut index = 0;
    while index + 110 <= data.len() {
        let magic = &data[index..index + 6];
        if magic != b"070701" && magic != b"070702" {
            return Err(Error::Target {
                message: "The RPM payload is not a newc cpio archive.".into(),
            });
        }
        let mode = parse_hex(&data[index + 14..index + 22])? as u32;
        let filesize = parse_hex(&data[index + 54..index + 62])? as usize;
        let namesize = parse_hex(&data[index + 94..index + 102])? as usize;
        if namesize == 0 {
            return Err(Error::Target {
                message: "The RPM payload has an empty file name.".into(),
            });
        }
        let name_start = index + 110;
        let name_end = name_start + namesize;
        if name_end > data.len() {
            return Err(Error::Target {
                message: "The RPM payload is truncated.".into(),
            });
        }
        let name_bytes = &data[name_start..name_end - 1];
        let name = String::from_utf8_lossy(name_bytes);
        let name_pad = (4 - ((110 + namesize) % 4)) % 4;
        let data_start = name_end + name_pad;
        let data_end = data_start + filesize;
        let data_pad = (4 - (filesize % 4)) % 4;
        let next = data_end + data_pad;
        if data_end > data.len() || next > data.len() {
            return Err(Error::Target {
                message: "The RPM payload is truncated.".into(),
            });
        }
        if name == "TRAILER!!!" {
            return Ok(());
        }
        let rel = normalize_rel(Path::new(name.as_ref()));
        let Some(out) = safe_join(dest, &rel) else {
            return Err(Error::Target {
                message: format!("The RPM contains an unsafe path ({name})."),
            });
        };
        let file_type = mode & 0o170000;
        if file_type == 0o040000 {
            fs::create_dir_all(&out).map_err(|err| Error::io(err, out.display()))?;
        } else if file_type == 0o100000 {
            if let Some(parent) = out.parent() {
                fs::create_dir_all(parent).map_err(|err| Error::io(err, parent.display()))?;
            }
            let mut file = File::create(&out).map_err(|err| Error::io(err, out.display()))?;
            io::copy(&mut &data[data_start..data_end], &mut file)
                .map_err(|err| Error::io(err, out.display()))?;
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                let perm = fs::Permissions::from_mode(mode & 0o777);
                fs::set_permissions(&out, perm).map_err(|err| Error::io(err, out.display()))?;
            }
        }
        index = next;
    }
    Err(Error::Target {
        message: "The RPM payload ended before the archive trailer.".into(),
    })
}

fn parse_hex(bytes: &[u8]) -> Result<u64, Error> {
    let text = std::str::from_utf8(bytes).map_err(|_| Error::Target {
        message: "The RPM payload has a non-text header.".into(),
    })?;
    u64::from_str_radix(text, 16).map_err(|_| Error::Target {
        message: "The RPM payload has a broken header field.".into(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    #[test]
    fn roundtrip_script() {
        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let dest = std::env::temp_dir().join(format!("linkspan-cpio-{nanos}"));
        let bytes = encode_newc(&[(
            "usr/bin/hello-linkspan",
            b"#!/bin/sh\nprintf 'ok\\n'\n",
            0o755,
        )]);
        extract_newc(std::io::Cursor::new(bytes), &dest).unwrap();
        let program = dest.join("usr/bin/hello-linkspan");
        assert_eq!(
            fs::read_to_string(program).unwrap(),
            "#!/bin/sh\nprintf 'ok\\n'\n"
        );
        let _ = fs::remove_dir_all(dest);
    }

    #[test]
    fn rejects_escape() {
        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let dest = std::env::temp_dir().join(format!("linkspan-cpio-bad-{nanos}"));
        let bytes = encode_newc(&[("../outside", b"nope\n", 0o644)]);
        let err = extract_newc(std::io::Cursor::new(bytes), &dest).unwrap_err();
        assert!(err.to_string().contains("unsafe"));
        let _ = fs::remove_dir_all(dest);
    }
}
