//! FAT32 SD card images of golden-corpus fixtures, for the host streaming scenario.
//!
//! The project tree (`SONGS/…`, `SAMPLES/…`) comes from `scripts/golden_mixdown.sh reconstruct`,
//! which rebuilds a fixture from the local `~/Deluge Backup` corpus. The image is packed with
//! mtools in a real card's geometry: 32 KB clusters and at least 2.5 GB, since FAT32 needs 65,525
//! clusters.
#![cfg(feature = "host_app")]

use std::path::{Path, PathBuf};
use std::process::Command;

/// Returns the read-only template image of `fixture`'s project tree, reconstructing the tree and
/// packing the image as needed.
///
/// The template is cached in the temp dir by content (see [`crate::storage::sd_image_path`]) and
/// must never be mounted: a run points `DELUGE_SD_IMAGE` at a
/// [`private_copy`](crate::storage::sd_image_path::private_copy) of it, before any SD access.
///
/// # Panics
///
/// Panics if the corpus or mtools is missing, or a step fails.
pub fn pack_golden_fixture(repo_root: &Path, fixture: &str) -> PathBuf {
    pack_image(&golden_fixture_tree(repo_root, fixture))
}

/// Returns the project tree for corpus fixture `fixture` (`<DELUGE_GOLDEN_DIR>/<fixture>_proj`),
/// reconstructing it with `scripts/golden_mixdown.sh reconstruct` if absent.
///
/// `DELUGE_GOLDEN_DIR` defaults to `~/.cache/deluge-golden`.
///
/// # Panics
///
/// Panics if the reconstruction fails.
pub fn golden_fixture_tree(repo_root: &Path, fixture: &str) -> PathBuf {
    let golden_dir = std::env::var("DELUGE_GOLDEN_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| {
            PathBuf::from(std::env::var("HOME").expect("HOME must be set"))
                .join(".cache/deluge-golden")
        });
    let proj = golden_dir.join(format!("{fixture}_proj"));

    if !proj.is_dir() {
        log::info!(
            "streaming-scenario: reconstructing '{fixture}' project tree at {} (scripts/golden_mixdown.sh reconstruct)",
            proj.display()
        );
        let status = Command::new("bash")
            .arg(repo_root.join("scripts/golden_mixdown.sh"))
            .arg("reconstruct")
            .env("FIXTURE", fixture)
            .current_dir(repo_root)
            .status()
            .expect("run scripts/golden_mixdown.sh reconstruct");
        assert!(
            status.success(),
            "scripts/golden_mixdown.sh reconstruct failed for fixture '{fixture}' — is DELUGE_BACKUP \
             (default '~/Deluge Backup') present?"
        );
        assert!(
            proj.is_dir(),
            "reconstruct did not produce {} — see the golden_mixdown.sh output above",
            proj.display()
        );
    }

    proj
}

/// Returns the read-only template image of `project_dir`, packing it unless a cached one for the
/// same contents exists.
///
/// Uses `mformat -c 64` for 32 KB clusters: mtools' size-based default picks 2 KB clusters, which
/// would make the firmware stream in far smaller clusters than on a real card.
///
/// # Panics
///
/// Panics if mtools fails or the image cannot be installed.
pub(crate) fn pack_image(project_dir: &Path) -> PathBuf {
    let fingerprint = crate::storage::sd_image_path::fingerprint_dir(project_dir);
    let mut packed = false;
    let img = crate::storage::sd_image_path::ensure_template(
        &std::env::temp_dir(),
        &fingerprint,
        |staging| {
            packed = true;
            let dir_bytes = dir_size_bytes(project_dir);
            let mut bytes = dir_bytes.saturating_mul(2) + (64u64 << 20);
            let min_bytes = 2560u64 << 20; // 2.5 GB: comfortably over FAT32's 65,525 32 KB clusters
            if bytes < min_bytes {
                bytes = min_bytes;
            }
            bytes = (bytes + 511) & !511u64;

            // Run through `sh` so the shell expands `mcopy`'s `*`.
            let script = format!(
                "set -e; truncate -s {bytes} '{staging}'; mformat -i '{staging}' -F -c 64 ::; \
                 mcopy -s -Q -i '{staging}' '{proj}'/* ::/",
                bytes = bytes,
                staging = staging.display(),
                proj = project_dir.display(),
            );
            let status = Command::new("sh").arg("-c").arg(&script).status()?;
            if status.success() {
                Ok(())
            } else {
                Err(std::io::Error::other(format!(
                    "is mtools (mformat/mcopy) installed? script: {script}"
                )))
            }
        },
    )
    .unwrap_or_else(|e| {
        panic!(
            "packing '{}' into a FAT image failed: {e}",
            project_dir.display()
        )
    });
    log::info!(
        "streaming-scenario: {} template for '{}' -> {} (fingerprint {fingerprint})",
        if packed { "packed" } else { "reusing cached" },
        project_dir.display(),
        img.display()
    );
    img
}

fn dir_size_bytes(dir: &Path) -> u64 {
    let out = Command::new("du")
        .arg("-sb")
        .arg(dir)
        .output()
        .expect("run du -sb");
    String::from_utf8_lossy(&out.stdout)
        .split_whitespace()
        .next()
        .and_then(|s| s.parse::<u64>().ok())
        .unwrap_or(0)
}
