//! `cargo test` entry point for the content-addressed SD-image naming policy
//! (`src/sd_image_path.rs`). Plain-host test: needs neither the `host_app`
//! feature nor a CMake C++ build (build.rs's host path is a no-op without
//! `host_app`), because the naming policy is deliberately split out of the
//! mtools-invoking `sd_image.rs`.
//!
//!   cargo test --test sd_image_path_host
#![cfg(not(target_os = "none"))]

#[path = "../src/sd_image_path.rs"]
mod sd_image_path;

use sd_image_path::{Plan, fingerprint_dir, recorder_image_path, scenario_plan};
use std::fs;
use std::path::{Path, PathBuf};

/// Per-test scratch root under the cargo target dir (never `/tmp` — these tests
/// exist because of a `/tmp` leak).
fn scratch(name: &str) -> PathBuf {
    let dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("target/tmp/sd_image_path_tests")
        .join(name);
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).expect("create scratch dir");
    dir
}

fn write_file(dir: &Path, name: &str, contents: &[u8]) {
    fs::write(dir.join(name), contents).expect("write fixture file");
}

// --- the leak regression: the reusable path must NOT be process-unique -------

#[test]
fn scenario_image_path_is_identical_for_the_same_fingerprint() {
    let tmp = scratch("stable_path");

    let a = scenario_plan(&tmp, "deadbeef").final_path().to_path_buf();
    let b = scenario_plan(&tmp, "deadbeef").final_path().to_path_buf();

    assert_eq!(
        a, b,
        "same fingerprint must map to one reusable image; a per-run path is the /tmp leak"
    );
}

#[test]
fn scenario_image_path_does_not_embed_the_process_id() {
    let tmp = scratch("no_pid");
    let pid = std::process::id().to_string();

    let path = scenario_plan(&tmp, "deadbeef").final_path().to_path_buf();

    assert!(
        !path.to_string_lossy().contains(&pid),
        "final image path {path:?} embeds pid {pid} — every run would leak a new image"
    );
}

#[test]
fn different_fingerprints_get_different_images() {
    let tmp = scratch("distinct");

    let a = scenario_plan(&tmp, "aaaa").final_path().to_path_buf();
    let b = scenario_plan(&tmp, "bbbb").final_path().to_path_buf();

    assert_ne!(a, b, "distinct corpora must not share one image file");
}

// --- reuse vs. repack --------------------------------------------------------

#[test]
fn plans_a_pack_when_no_image_exists_yet() {
    let tmp = scratch("pack_when_absent");

    match scenario_plan(&tmp, "deadbeef") {
        Plan::Pack { .. } => {}
        Plan::Reuse(p) => panic!("nothing packed yet, but plan said reuse {p:?}"),
    }
}

#[test]
fn reuses_an_already_packed_image_instead_of_repacking() {
    let tmp = scratch("reuse_when_present");
    let target = scenario_plan(&tmp, "deadbeef").final_path().to_path_buf();
    fs::write(&target, b"already packed").expect("pre-create image");

    match scenario_plan(&tmp, "deadbeef") {
        Plan::Reuse(p) => assert_eq!(p, target),
        Plan::Pack { .. } => {
            panic!("image {target:?} already exists — repacking is what fills /tmp")
        }
    }
}

#[test]
fn staging_path_is_process_unique_so_concurrent_packs_do_not_collide() {
    let tmp = scratch("staging_unique");
    let pid = std::process::id().to_string();

    let Plan::Pack {
        staging,
        final_path,
    } = scenario_plan(&tmp, "deadbeef")
    else {
        panic!("expected a pack plan for an absent image");
    };

    assert!(
        staging.to_string_lossy().contains(&pid),
        "staging path {staging:?} must be process-unique so parallel packs don't corrupt each other"
    );
    assert_ne!(
        staging, final_path,
        "staging must differ from the final path so the install can be an atomic rename"
    );
}

// --- fingerprinting ----------------------------------------------------------

#[test]
fn fingerprint_is_stable_for_an_unchanged_tree() {
    let dir = scratch("fp_stable");
    write_file(&dir, "song.XML", b"<song/>");

    assert_eq!(fingerprint_dir(&dir), fingerprint_dir(&dir));
}

#[test]
fn fingerprint_changes_when_a_sample_is_added() {
    let dir = scratch("fp_added");
    write_file(&dir, "song.XML", b"<song/>");
    let before = fingerprint_dir(&dir);

    write_file(&dir, "kick.wav", b"RIFF....");

    assert_ne!(
        before,
        fingerprint_dir(&dir),
        "a corpus change must produce a new image rather than silently reusing a stale one"
    );
}

#[test]
fn fingerprint_changes_when_a_sample_changes_size() {
    let dir = scratch("fp_resized");
    write_file(&dir, "kick.wav", b"RIFF");
    let before = fingerprint_dir(&dir);

    write_file(&dir, "kick.wav", b"RIFF-but-longer-now");

    assert_ne!(
        before,
        fingerprint_dir(&dir),
        "edited sample must invalidate the cached image"
    );
}

// --- the sibling leak: format_empty_image ------------------------------------

#[test]
fn recorder_image_path_does_not_embed_the_process_id() {
    let tmp = scratch("recorder_no_pid");
    let pid = std::process::id().to_string();

    let path = recorder_image_path(&tmp);

    assert!(
        !path.to_string_lossy().contains(&pid),
        "recorder image path {path:?} embeds pid {pid} — this is the same leak as pack_image"
    );
}

#[test]
fn recorder_image_path_is_a_single_stable_file() {
    let tmp = scratch("recorder_stable");

    assert_eq!(recorder_image_path(&tmp), recorder_image_path(&tmp));
}
