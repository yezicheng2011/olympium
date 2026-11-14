// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

use crate::error::Error;
use std::path::{Component, Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

pub fn unix_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as u64)
        .unwrap_or(0)
}

pub fn reject_bad_text(label: &str, value: &str) -> Result<(), Error> {
    if value.chars().any(|c| c == '\0' || c == '\n' || c == '\r') {
        return Err(Error::Target {
            message: format!("{label} contains a newline or a null character."),
        });
    }
    Ok(())
}

pub fn reject_url(value: &str) -> Result<(), Error> {
    let trimmed = value.trim();
    let lower = trimmed.to_ascii_lowercase();
    if lower.contains("://")
        || lower.starts_with("http:")
        || lower.starts_with("https:")
        || lower.starts_with("market:")
        || lower.starts_with("file:")
    {
        return Err(Error::Target {
            message: "Olympium does not download programs. Add a file on this computer, or the name of an app that is already installed.".into(),
        });
    }
    Ok(())
}

pub fn valid_dotted_id(value: &str) -> bool {
    if value.len() < 3 || value.len() > 255 || !value.contains('.') {
        return false;
    }
    let mut count = 0;
    for part in value.split('.') {
        count += 1;
        let mut chars = part.chars();
        let Some(first) = chars.next() else {
            return false;
        };
        if !first.is_ascii_alphabetic() {
            return false;
        }
        if !chars.all(|c| c.is_ascii_alphanumeric() || c == '_') {
            return false;
        }
    }
    count >= 2
}

pub fn valid_bundle_id(value: &str) -> bool {
    if value.len() < 3 || value.len() > 255 || !value.contains('.') || value.starts_with('-') {
        return false;
    }
    for part in value.split('.') {
        let mut chars = part.chars();
        let Some(first) = chars.next() else {
            return false;
        };
        if !first.is_ascii_alphabetic() {
            return false;
        }
        if !chars.all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_') {
            return false;
        }
    }
    true
}

pub fn valid_flatpak_ref(value: &str) -> bool {
    if value.is_empty() || value.len() > 200 || value.starts_with('-') || !value.contains('.') {
        return false;
    }
    if value.contains("..") || value.contains('\\') || value.contains(' ') {
        return false;
    }
    value
        .chars()
        .all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '-' | '/'))
}

pub fn valid_snap_name(value: &str) -> bool {
    let mut chars = value.chars();
    match chars.next() {
        Some(c) if c.is_ascii_lowercase() || c.is_ascii_digit() => {}
        _ => return false,
    }
    !value.is_empty()
        && value.len() <= 64
        && value
            .chars()
            .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '-')
}

pub fn valid_simple_name(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 64
        && value
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_' || c == '.')
}

/// Join `rel` onto `root` and refuse absolute paths, prefixes, and `..`.
pub fn safe_join(root: &Path, rel: &Path) -> Option<PathBuf> {
    if rel.as_os_str().is_empty() {
        return None;
    }
    let mut out = root.to_path_buf();
    let mut any = false;
    for component in rel.components() {
        match component {
            Component::Normal(part) => {
                if part.is_empty() || part == "." || part == ".." {
                    return None;
                }
                out.push(part);
                any = true;
            }
            Component::CurDir => {}
            Component::ParentDir | Component::RootDir | Component::Prefix(_) => return None,
        }
    }
    if any {
        Some(out)
    } else {
        None
    }
}

pub fn normalize_rel(path: &Path) -> PathBuf {
    let text = path.to_string_lossy();
    let text = text.strip_prefix("./").unwrap_or(&text);
    PathBuf::from(text)
}

pub fn read_prefix(path: &Path, limit: usize) -> Result<Vec<u8>, Error> {
    use std::io::Read;
    let mut file = std::fs::File::open(path).map_err(|err| Error::io(err, path.display()))?;
    let mut buf = vec![0u8; limit];
    let n = file
        .read(&mut buf)
        .map_err(|err| Error::io(err, path.display()))?;
    buf.truncate(n);
    Ok(buf)
}

pub fn extension_lower(path: &Path) -> String {
    path.extension()
        .map(|ext| ext.to_string_lossy().to_ascii_lowercase())
        .unwrap_or_default()
}

pub fn parent_string(path: &str) -> Option<String> {
    let parent = Path::new(path).parent()?;
    if parent.as_os_str().is_empty() {
        None
    } else {
        Some(parent.to_string_lossy().into_owned())
    }
}

pub fn lossy(bytes: &[u8]) -> String {
    String::from_utf8_lossy(bytes).into_owned()
}

pub fn slug(name: &str) -> String {
    let mut out = String::new();
    let mut dash = false;
    for ch in name.chars() {
        if ch.is_ascii_alphanumeric() {
            out.push(ch.to_ascii_lowercase());
            dash = false;
        } else if !dash && !out.is_empty() {
            out.push('-');
            dash = true;
        }
        if out.len() >= 32 {
            break;
        }
    }
    let out = out.trim_matches('-').to_string();
    if out.is_empty() {
        "program".to_string()
    } else {
        out
    }
}

pub fn new_id(name: &str) -> String {
    use std::sync::atomic::{AtomicU64, Ordering};
    static COUNTER: AtomicU64 = AtomicU64::new(1);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    format!("{}-{:x}{:x}", slug(name), unix_ms(), n)
}

/// Translate `C:\Apps\Hello` into `/mnt/c/Apps/Hello`.
pub fn windows_path_to_wsl(raw: &str) -> Option<String> {
    let mut text = raw.replace('/', "\\");
    if let Some(rest) = text.strip_prefix(r"\\?\") {
        text = rest.to_string();
    }
    let bytes = text.as_bytes();
    if bytes.len() >= 2 && bytes[0].is_ascii_alphabetic() && bytes[1] == b':' {
        let drive = (bytes[0] as char).to_ascii_lowercase();
        let rest = text[2..].replace('\\', "/");
        return Some(format!("/mnt/{drive}{rest}"));
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wsl_path_translation() {
        assert_eq!(
            windows_path_to_wsl(r"C:\Apps\Hello"),
            Some("/mnt/c/Apps/Hello".into())
        );
        assert_eq!(
            windows_path_to_wsl(r"\\?\D:\Games\a.exe"),
            Some("/mnt/d/Games/a.exe".into())
        );
        assert_eq!(windows_path_to_wsl("/usr/bin/true"), None);
    }

    #[test]
    fn safe_join_rejects_escape() {
        let root = Path::new("/tmp/olympium");
        assert!(safe_join(root, Path::new("usr/bin/hello")).is_some());
        assert!(safe_join(root, Path::new("../etc/passwd")).is_none());
        assert!(safe_join(root, Path::new("/etc/passwd")).is_none());
        assert!(safe_join(root, Path::new("usr/bin/../../outside")).is_none());
    }
}
