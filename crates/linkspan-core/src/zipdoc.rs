// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

//! Read-only zip index for package classification.
//! Linkspan lists names and, when needed, inflates one entry in memory.
//! It does not rewrite archives, strip signatures, or add certificates.

use crate::error::Error;
use flate2::read::DeflateDecoder;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

const LOCAL: [u8; 4] = [0x50, 0x4b, 0x03, 0x04];
const CENTRAL: [u8; 4] = [0x50, 0x4b, 0x01, 0x02];
const EOCD: [u8; 4] = [0x50, 0x4b, 0x05, 0x06];
const MAX_ENTRY: u64 = 32 * 1024 * 1024;

#[derive(Debug, Clone)]
pub struct ZipEntry {
    pub name: String,
    pub method: u16,
    local_offset: u64,
    compressed_size: u64,
    uncompressed_size: u64,
}

pub fn list_file(path: &Path) -> Result<Vec<ZipEntry>, Error> {
    let mut file = File::open(path).map_err(|err| Error::io(err, path.display()))?;
    let length = file
        .metadata()
        .map_err(|err| Error::io(err, path.display()))?
        .len();
    let tail_len = length.min(65_557);
    file.seek(SeekFrom::Start(length - tail_len))
        .map_err(|err| Error::io(err, path.display()))?;
    let mut tail = vec![0u8; tail_len as usize];
    file.read_exact(&mut tail)
        .map_err(|err| Error::io(err, path.display()))?;
    let eocd_at = find_eocd(&tail).ok_or_else(|| Error::Target {
        message: format!("{} is not a zip package.", path.display()),
    })?;
    let eocd = &tail[eocd_at..];
    if eocd.len() < 22 {
        return Err(Error::Target {
            message: format!("{} is not a zip package.", path.display()),
        });
    }
    let cd_size = u32::from_le_bytes(eocd[12..16].try_into().unwrap()) as u64;
    let cd_offset = u32::from_le_bytes(eocd[16..20].try_into().unwrap()) as u64;
    if cd_size > 16 * 1024 * 1024 || cd_offset.saturating_add(cd_size) > length {
        return Err(Error::Target {
            message: format!(
                "{} has a zip directory Linkspan will not read.",
                path.display()
            ),
        });
    }
    file.seek(SeekFrom::Start(cd_offset))
        .map_err(|err| Error::io(err, path.display()))?;
    let mut directory = vec![0u8; cd_size as usize];
    file.read_exact(&mut directory)
        .map_err(|err| Error::io(err, path.display()))?;
    parse_central(&directory)
}

pub fn list_bytes(bytes: &[u8]) -> Result<Vec<ZipEntry>, Error> {
    let eocd_at = find_eocd(bytes).ok_or_else(|| Error::Target {
        message: "Nested package is not a zip.".into(),
    })?;
    let eocd = &bytes[eocd_at..];
    if eocd.len() < 22 {
        return Err(Error::Target {
            message: "Nested package is not a zip.".into(),
        });
    }
    let cd_size = u32::from_le_bytes(eocd[12..16].try_into().unwrap()) as usize;
    let cd_offset = u32::from_le_bytes(eocd[16..20].try_into().unwrap()) as usize;
    if cd_offset.saturating_add(cd_size) > bytes.len() {
        return Err(Error::Target {
            message: "Nested package has a zip directory Linkspan will not read.".into(),
        });
    }
    parse_central(&bytes[cd_offset..cd_offset + cd_size])
}

pub fn read_entry(path: &Path, entry: &ZipEntry) -> Result<Vec<u8>, Error> {
    if entry.uncompressed_size > MAX_ENTRY || entry.compressed_size > MAX_ENTRY {
        return Err(Error::Target {
            message: format!("{} is too large to inspect.", entry.name),
        });
    }
    let mut file = File::open(path).map_err(|err| Error::io(err, path.display()))?;
    file.seek(SeekFrom::Start(entry.local_offset))
        .map_err(|err| Error::io(err, path.display()))?;
    let mut header = [0u8; 30];
    file.read_exact(&mut header)
        .map_err(|err| Error::io(err, path.display()))?;
    if header[0..4] != LOCAL {
        return Err(Error::Target {
            message: format!("{} is not a readable zip entry.", entry.name),
        });
    }
    let name_len = u16::from_le_bytes([header[26], header[27]]) as u64;
    let extra_len = u16::from_le_bytes([header[28], header[29]]) as u64;
    file.seek(SeekFrom::Current((name_len + extra_len) as i64))
        .map_err(|err| Error::io(err, path.display()))?;
    let mut compressed = vec![0u8; entry.compressed_size as usize];
    file.read_exact(&mut compressed)
        .map_err(|err| Error::io(err, path.display()))?;
    inflate(entry.method, &compressed, entry.uncompressed_size as usize)
}

pub fn read_entry_bytes(archive: &[u8], entry: &ZipEntry) -> Result<Vec<u8>, Error> {
    if entry.uncompressed_size > MAX_ENTRY || entry.compressed_size > MAX_ENTRY {
        return Err(Error::Target {
            message: format!("{} is too large to inspect.", entry.name),
        });
    }
    let start = entry.local_offset as usize;
    if start + 30 > archive.len() {
        return Err(Error::Target {
            message: format!("{} is outside the package.", entry.name),
        });
    }
    let header = &archive[start..start + 30];
    if header[0..4] != LOCAL {
        return Err(Error::Target {
            message: format!("{} is not a readable zip entry.", entry.name),
        });
    }
    let name_len = u16::from_le_bytes([header[26], header[27]]) as usize;
    let extra_len = u16::from_le_bytes([header[28], header[29]]) as usize;
    let data_at = start + 30 + name_len + extra_len;
    let data_end = data_at + entry.compressed_size as usize;
    if data_end > archive.len() {
        return Err(Error::Target {
            message: format!("{} is outside the package.", entry.name),
        });
    }
    inflate(
        entry.method,
        &archive[data_at..data_end],
        entry.uncompressed_size as usize,
    )
}

fn inflate(method: u16, compressed: &[u8], uncompressed: usize) -> Result<Vec<u8>, Error> {
    inflate_limited(method, compressed, uncompressed, MAX_ENTRY)
}

fn inflate_limited(
    method: u16,
    compressed: &[u8],
    uncompressed: usize,
    max_entry: u64,
) -> Result<Vec<u8>, Error> {
    let plain = match method {
        0 => {
            if compressed.len() as u64 > max_entry {
                return Err(too_large());
            }
            compressed.to_vec()
        }
        8 => {
            let mut decoder = DeflateDecoder::new(compressed);
            let mut out = Vec::new();
            let mut limited = (&mut decoder).take(max_entry + 1);
            limited.read_to_end(&mut out).map_err(|err| Error::Target {
                message: format!("Could not read a compressed zip entry: {err}"),
            })?;
            if out.len() as u64 > max_entry {
                return Err(too_large());
            }
            out
        }
        _ => {
            return Err(Error::Target {
                message: "This zip uses a compression method Linkspan does not read.".into(),
            });
        }
    };
    if plain.len() != uncompressed && uncompressed != 0 {
        return Err(Error::Target {
            message: "A zip entry size does not match its directory.".into(),
        });
    }
    Ok(plain)
}

fn too_large() -> Error {
    Error::Target {
        message: "A zip entry is too large to inspect.".into(),
    }
}

#[cfg(test)]
mod tests {
    use super::inflate_limited;
    use flate2::write::DeflateEncoder;
    use flate2::Compression;
    use std::io::Write;

    #[test]
    fn deflate_output_stops_at_the_cap() {
        let mut encoder = DeflateEncoder::new(Vec::new(), Compression::fast());
        encoder.write_all(&[0u8; 200]).unwrap();
        let compressed = encoder.finish().unwrap();
        let err = inflate_limited(8, &compressed, 200, 32).unwrap_err();
        assert!(err.message().contains("too large"));
    }
}

fn parse_central(directory: &[u8]) -> Result<Vec<ZipEntry>, Error> {
    let mut entries = Vec::new();
    let mut cursor = 0;
    while cursor + 46 <= directory.len() {
        if directory[cursor..cursor + 4] != CENTRAL {
            break;
        }
        let method = u16::from_le_bytes([directory[cursor + 10], directory[cursor + 11]]);
        let compressed_size =
            u32::from_le_bytes(directory[cursor + 20..cursor + 24].try_into().unwrap()) as u64;
        let uncompressed_size =
            u32::from_le_bytes(directory[cursor + 24..cursor + 28].try_into().unwrap()) as u64;
        let name_len =
            u16::from_le_bytes([directory[cursor + 28], directory[cursor + 29]]) as usize;
        let extra_len =
            u16::from_le_bytes([directory[cursor + 30], directory[cursor + 31]]) as usize;
        let comment_len =
            u16::from_le_bytes([directory[cursor + 32], directory[cursor + 33]]) as usize;
        let local_offset =
            u32::from_le_bytes(directory[cursor + 42..cursor + 46].try_into().unwrap()) as u64;
        let name_at = cursor + 46;
        let name_end = name_at + name_len;
        if name_end > directory.len() {
            return Err(Error::Target {
                message: "The zip directory is truncated.".into(),
            });
        }
        let name = String::from_utf8_lossy(&directory[name_at..name_end]).into_owned();
        entries.push(ZipEntry {
            name,
            method,
            local_offset,
            compressed_size,
            uncompressed_size,
        });
        cursor = name_end + extra_len + comment_len;
    }
    Ok(entries)
}

fn find_eocd(bytes: &[u8]) -> Option<usize> {
    if bytes.len() < 22 {
        return None;
    }
    let start = bytes.len() - 22;
    for index in (0..=start).rev() {
        if bytes[index..index + 4] == EOCD {
            let comment = u16::from_le_bytes([bytes[index + 20], bytes[index + 21]]) as usize;
            if index + 22 + comment == bytes.len() {
                return Some(index);
            }
        }
    }
    None
}
