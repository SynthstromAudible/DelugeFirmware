//! Content-addressed naming for the harness's temporary FAT32 SD images, and each run's private
//! copy of one.
//!
//! Kept apart from `storage::sd_image`, which needs `host_app`, so the policy is testable in a
//! plain host build. Linux only: the copy shells out to GNU `cp` and lives behind `/proc/self/fd`.
//!
//! Images are keyed on a fingerprint of the packed corpus: one template per distinct corpus, packed
//! once under a lock, reused by later runs and repacked when the corpus changes, so the temp dir
//! stays bounded. A template is read-only and never mounted: a run that wrote to it would hand every
//! later run a card holding its recordings and saves, which the fingerprint (of the source tree)
//! cannot notice. Each run mounts a [`private_copy`] instead.

use std::fs::{File, OpenOptions};
use std::io;
use std::os::fd::IntoRawFd;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicU32, Ordering};

/// Whether an image for a corpus fingerprint can be reused or must be packed.
pub enum Plan {
    /// A matching template is already on disk; reuse it and skip mtools entirely.
    Reuse(PathBuf),
    /// No matching image yet: pack into `staging`, then rename onto `final_path`.
    ///
    /// `staging` is process-unique and the rename is atomic, so a partial image is never visible
    /// at the shared path. [`ensure_template`] holds a lock across the plan and the pack.
    Pack {
        staging: PathBuf,
        final_path: PathBuf,
    },
}

impl Plan {
    /// Returns the template's path once packed.
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

/// Returns the read-only template image for `fingerprint`, packing it first if no process has.
///
/// `pack` fills the staging path it is given with the image. An exclusive `flock` on a per-
/// fingerprint lock file serialises the check and the pack across processes, so concurrent runs
/// of one corpus pack it exactly once and the others reuse it. The staging file is made read-only
/// and then renamed into place, so no process ever sees a partial or writable template. A staging
/// file left by a packer that died is removed before packing.
///
/// # Errors
///
/// Returns an error if the lock, `pack`, or installing the image fails; the staging file is
/// removed first.
pub fn ensure_template(
    tmp_root: &Path,
    fingerprint: &str,
    pack: impl FnOnce(&Path) -> io::Result<()>,
) -> io::Result<PathBuf> {
    let lock = OpenOptions::new()
        .create(true)
        .truncate(false)
        .write(true)
        .open(tmp_root.join(format!("deluge-streaming-scenario-{fingerprint}.lock")))?;
    // Released when `lock` closes; a run that dies holding it releases it with its fds.
    lock.lock()?;
    let (staging, final_path) = match scenario_plan(tmp_root, fingerprint) {
        Plan::Reuse(img) => return Ok(img),
        Plan::Pack {
            staging,
            final_path,
        } => (staging, final_path),
    };
    // Under the lock, any staging file for this fingerprint belongs to a packer that died.
    let stale_prefix = format!("deluge-streaming-scenario-{fingerprint}.");
    for entry in std::fs::read_dir(tmp_root)?.flatten() {
        let name = entry.file_name();
        let name = name.to_string_lossy();
        if name.starts_with(&stale_prefix) && name.ends_with(".part") {
            let _ = std::fs::remove_file(entry.path());
        }
    }
    let installed = pack(&staging)
        .and_then(|()| {
            let mut perms = std::fs::metadata(&staging)?.permissions();
            perms.set_readonly(true);
            std::fs::set_permissions(&staging, perms)
        })
        .and_then(|()| std::fs::rename(&staging, &final_path));
    if let Err(e) = installed {
        let _ = std::fs::remove_file(&staging);
        return Err(e);
    }
    Ok(final_path)
}

/// Returns a path to a new writable copy of `template` that only this process can reach, and that
/// the kernel frees when the process exits.
///
/// The copy is made beside the template with `cp --reflink=auto --sparse=always`, which keeps the
/// image's holes (`std::fs::copy` writes them out in full on tmpfs: 2.5 GB per run), opened, and
/// unlinked at once. The returned `/proc/self/fd/<n>` path reopens that unlinked file, so a run
/// mounts it by path like any image while nothing is left on disk for anyone to clean up: the
/// harness exits through `_exit`, so no destructor could remove a named copy, and a run killed by
/// a timeout would leak one. The fd is deliberately never closed.
///
/// # Errors
///
/// Returns an error if the copy or opening it fails; a copy already made is removed.
pub fn private_copy(template: &Path) -> io::Result<PathBuf> {
    static SEQ: AtomicU32 = AtomicU32::new(0);
    let dir = template.parent().unwrap_or(Path::new("."));
    let copy = dir.join(format!(
        "deluge-streaming-run-{}-{}.img",
        std::process::id(),
        SEQ.fetch_add(1, Ordering::Relaxed)
    ));
    let opened = Command::new("cp")
        .args(["--reflink=auto", "--sparse=always", "--"])
        .arg(template)
        .arg(&copy)
        .status()
        .and_then(|status| {
            if status.success() {
                Ok(())
            } else {
                Err(io::Error::other(format!("cp exited with {status}")))
            }
        })
        .and_then(|()| {
            // `cp` gives the copy the template's read-only mode.
            std::fs::set_permissions(&copy, std::fs::Permissions::from_mode(0o600))
        })
        .and_then(|()| OpenOptions::new().read(true).write(true).open(&copy));
    let _ = std::fs::remove_file(&copy);
    let file: File = opened?;
    Ok(PathBuf::from(format!(
        "/proc/self/fd/{}",
        file.into_raw_fd()
    )))
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
