//! Content-addressed naming for the harness's temporary FAT32 SD images.
//!
//! Kept apart from `storage::sd_image`, which needs `host_app`, so the naming policy is
//! testable in a plain host build.
//!
//! Images are keyed on a fingerprint of the packed corpus: one image per distinct corpus, reused by
//! later runs and repacked when the corpus changes, so the temp dir stays bounded. They are not
//! deleted at exit, because packing and consuming an image in the same process is unreliable on
//! some hosts (see `harness/preemptive_race_tsan/run.sh`); the reliable workflow reuses an image
//! packed by an earlier process.

#![cfg(not(target_os = "none"))]
#![allow(dead_code)]

use std::path::{Path, PathBuf};

/// Whether an image for a corpus fingerprint can be reused or must be packed.
pub enum Plan {
    /// A matching image is already on disk; use it as-is and skip mtools entirely.
    Reuse(PathBuf),
    /// No matching image yet: pack into `staging`, then rename onto `final_path`.
    ///
    /// `staging` is process-unique and the rename is atomic, so a partial image is never visible
    /// at the shared path and concurrent packs of the same corpus race harmlessly.
    Pack {
        staging: PathBuf,
        final_path: PathBuf,
    },
}

impl Plan {
    /// Returns the path the caller points `DELUGE_SD_IMAGE` at.
    pub fn final_path(&self) -> &Path {
        match self {
            Plan::Reuse(p) => p,
            Plan::Pack { final_path, .. } => final_path,
        }
    }
}

/// Decides whether the image for `fingerprint` can be reused or must be packed.
pub fn scenario_plan(tmp_root: &Path, fingerprint: &str) -> Plan {
    let final_path = tmp_root.join(format!("deluge-streaming-scenario-{fingerprint}.img"));
    if final_path.is_file() {
        return Plan::Reuse(final_path);
    }
    let staging = tmp_root.join(format!(
        "deluge-streaming-scenario-{fingerprint}.{}.part",
        std::process::id()
    ));
    Plan::Pack {
        staging,
        final_path,
    }
}

/// Returns a hex fingerprint of `dir`'s contents: every file's relative path, length and mtime,
/// hashed in sorted order.
///
/// Metadata-only, like `make`'s staleness test: hashing gigabytes of audio would cost more than
/// the pack it avoids. An edit that preserves length and mtime goes unnoticed; delete the cached
/// image to force a repack. Unreadable entries are hashed as a marker, so a permissions change
/// still invalidates the cache.
pub fn fingerprint_dir(dir: &Path) -> String {
    let mut entries = Vec::new();
    collect(dir, dir, &mut entries);
    entries.sort();

    // FNV-1a rather than `DefaultHasher`, whose output may change between toolchains and would
    // orphan every cached image.
    let mut hash: u64 = 0xcbf2_9ce4_8422_2325;
    for entry in &entries {
        for byte in entry.as_bytes() {
            hash ^= u64::from(*byte);
            hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
        }
    }
    format!("{hash:016x}")
}

/// Appends one `"<relative path>:<len>:<mtime>"` record per file beneath `dir`, recursively.
fn collect(root: &Path, dir: &Path, out: &mut Vec<String>) {
    let Ok(read_dir) = std::fs::read_dir(dir) else {
        out.push(format!(
            "{}:<unreadable-dir>",
            dir.strip_prefix(root).unwrap_or(dir).display()
        ));
        return;
    };
    for entry in read_dir.flatten() {
        let path = entry.path();
        let rel = path
            .strip_prefix(root)
            .unwrap_or(&path)
            .display()
            .to_string();
        match entry.metadata() {
            Ok(meta) if meta.is_dir() => collect(root, &path, out),
            Ok(meta) => {
                let mtime = meta
                    .modified()
                    .ok()
                    .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
                    .map_or(0, |d| d.as_nanos());
                out.push(format!("{rel}:{}:{mtime}", meta.len()));
            }
            Err(_) => out.push(format!("{rel}:<unreadable>")),
        }
    }
}
