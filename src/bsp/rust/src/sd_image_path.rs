//! Content-addressed naming for the harness's temporary FAT32 SD images.
//!
//! Split out of [`crate::sd_image`] (which shells out to mtools and is therefore
//! `host_app`-gated) so this policy — the part that decides *where* an image lives and
//! *whether* one must be built at all — stays plain-host testable with no CMake C++ build.
//! See `tests/sd_image_path_host.rs`.
//!
//! # Why content-addressed
//!
//! These images used to be named `deluge-streaming-scenario-<pid>.img`, created and then
//! simply abandoned: nothing in the tree ever unlinked one. Because the name keyed on
//! `std::process::id()`, every invocation minted a *new* file, so `/tmp` grew without
//! bound — twice to the point of incident (see `lens1_vt_sim/sweep.sh`'s note on the 11 GiB
//! event, and a later ~12 GiB recurrence).
//!
//! The obvious fix — an RAII guard that unlinks on drop — is *wrong here*, because
//! `preemptive_race_tsan/run.sh` documents that packing an image and consuming it as the SD
//! backing store within the SAME process is unreliable on some hosts (the scenario reports
//! `load_dispatched=false`); the reliable workflow is to pack in one process and reuse the
//! image from a later one. Unlinking at exit would destroy exactly the artifact the next run
//! is supposed to reuse.
//!
//! So the contradiction the old scheme sat on — a per-PID name (says "disposable, mine") with
//! a cross-process lifetime (says "cacheable, shared") — is resolved in favour of *shared*:
//! the filename keys on a fingerprint of the packed corpus instead of the PID. One image per
//! distinct corpus, reused by every later run, repacked automatically when the corpus changes.
//! Bounded instead of unbounded, and re-runs get faster because they skip the pack entirely.
//! This mirrors what `sweep.sh`'s `pack_selftest_image` already does for its own image.

#![cfg(not(target_os = "none"))]
// Shared by `#[path]` inclusion into the harness crates (golden_vt_render, lens1_vt_sim) as
// well as the parent bin, and exercised by tests/sd_image_path_host.rs. Any single one of
// those consumers uses only part of the surface — `recorder_image_path` for instance is
// reached only from `format_empty_image`, which the bin itself never calls — so per-crate
// dead-code analysis flags items that are live elsewhere. Same rationale as efatfs_core.rs.
#![allow(dead_code)]

use std::path::{Path, PathBuf};

/// What [`scenario_plan`] decided for a given corpus fingerprint.
pub(crate) enum Plan {
    /// A matching image is already on disk; use it as-is and skip mtools entirely.
    Reuse(PathBuf),
    /// No matching image yet. Pack into `staging`, then rename onto `final_path`.
    ///
    /// The two-step exists so a half-written image is never visible at the shared,
    /// content-addressed path: `staging` is process-unique, and `rename(2)` within one
    /// directory is atomic, so concurrent packs of the same corpus can race harmlessly —
    /// each writes its own staging file and the last rename wins, with every observer
    /// seeing either no image or a complete one.
    Pack {
        staging: PathBuf,
        final_path: PathBuf,
    },
}

impl Plan {
    /// The path the caller should ultimately point `DELUGE_SD_IMAGE` at.
    pub(crate) fn final_path(&self) -> &Path {
        match self {
            Plan::Reuse(p) => p,
            Plan::Pack { final_path, .. } => final_path,
        }
    }
}

/// Decides whether the image for `fingerprint` can be reused or must be packed.
pub(crate) fn scenario_plan(tmp_root: &Path, fingerprint: &str) -> Plan {
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

/// The single shared path for the recorder-roundtrip scenario's empty image.
///
/// Unlike [`scenario_plan`] this one is deliberately NOT reused across runs: that scenario
/// records into the image and then reads back what it wrote, so a stale image carrying a
/// previous run's recordings could mask a failure. The caller re-formats it every time —
/// `mformat` over an already-sparse file is cheap, and resetting the FAT is exactly what
/// "empty image" means. The win here is only that the file is *bounded*: one path, reused in
/// place, instead of one abandoned 2.5 GB sparse image per PID.
///
/// Runs that genuinely need isolation (parallel recorder invocations) should set
/// `DELUGE_SD_IMAGE` explicitly — every caller honours it and skips image creation entirely,
/// the same escape hatch `sweep.sh` uses.
pub(crate) fn recorder_image_path(tmp_root: &Path) -> PathBuf {
    tmp_root.join("deluge-recorder-roundtrip.img")
}

/// A cheap fingerprint of `dir`'s contents: every file's path-relative name, byte length and
/// mtime, hashed in a deterministic order.
///
/// Deliberately metadata-only. The corpus this addresses is multi-gigabyte of audio, so
/// hashing file *contents* would cost more than the mtools pack it is meant to avoid — this is
/// the same size+mtime staleness test `make` and `rsync` default to. The failure mode it
/// admits (an edit that preserves both length and mtime) requires deliberate effort, and the
/// escape hatch is to delete the cached image or point `DELUGE_SD_IMAGE` elsewhere.
///
/// Returns a stable hex string; unreadable entries are folded in as a distinct marker rather
/// than skipped, so a permissions change still invalidates the cache.
pub(crate) fn fingerprint_dir(dir: &Path) -> String {
    let mut entries = Vec::new();
    collect(dir, dir, &mut entries);
    entries.sort();

    // FNV-1a, written out rather than using `DefaultHasher` so the value is stable across
    // toolchain versions — a cache key that silently changes under a compiler upgrade would
    // orphan every previously packed image in /tmp, which is the bug this module exists to fix.
    let mut hash: u64 = 0xcbf2_9ce4_8422_2325;
    for entry in &entries {
        for byte in entry.as_bytes() {
            hash ^= u64::from(*byte);
            hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
        }
    }
    format!("{hash:016x}")
}

/// Appends one `"<relative path>:<len>:<mtime>"` record per file beneath `dir`, recursing into
/// subdirectories (the corpus is `SONGS/…` + `SAMPLES/…`, so a flat listing would miss almost
/// everything).
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
