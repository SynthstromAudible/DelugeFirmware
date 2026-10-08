//! The content-addressed SD-image policy (`storage::sd_image_path`): stable, reusable template
//! paths, reuse versus repack, packing once under concurrency, each run's private copy, and
//! fingerprinting. Runs in a plain host build.

use deluge_bsp_rust::storage::sd_image_path;

use sd_image_path::{Plan, ensure_template, fingerprint_dir, private_copy, scenario_plan};
use std::fs;
use std::io::{Read, Seek, SeekFrom, Write};
use std::os::unix::fs::MetadataExt;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU32, Ordering};

/// Returns a fresh per-test scratch directory under the cargo target dir, not `/tmp`.
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

// The leak regression: the reusable path must not be process-unique.

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

// --- the template and each run's private copy -------------------------------

/// A sparse 64 MB stand-in for a packed template: a marked first sector, holes after, read-only.
fn sparse_template(dir: &Path) -> PathBuf {
    let path = dir.join("template.img");
    let mut file = fs::File::create(&path).expect("create template");
    file.write_all(&[0xA5; 512])
        .expect("write the first sector");
    file.set_len(64 << 20).expect("size the template");
    drop(file);
    let mut perms = fs::metadata(&path).expect("stat template").permissions();
    perms.set_readonly(true);
    fs::set_permissions(&path, perms).expect("make the template read-only");
    path
}

/// Opens a card path the way `deluge_bsp::sd`'s host backing does.
fn open_like_the_card(path: &Path) -> fs::File {
    fs::OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .open(path)
        .unwrap_or_else(|e| panic!("the card cannot open {path:?}: {e}"))
}

fn first_sector(path: &Path) -> Vec<u8> {
    let mut buf = vec![0u8; 512];
    fs::File::open(path)
        .expect("open for read")
        .read_exact(&mut buf)
        .expect("read the first sector");
    buf
}

#[test]
fn a_run_writes_to_its_copy_and_never_to_the_template() {
    let tmp = scratch("copy_isolates_template");
    let template = sparse_template(&tmp);
    let before = fs::read(&template).expect("read template");

    let card = private_copy(&template).expect("copy");
    assert_ne!(card, template, "the run was handed the template itself");
    assert_eq!(
        first_sector(&card),
        vec![0xA5; 512],
        "the copy holds the template's contents"
    );
    let mut file = open_like_the_card(&card);
    file.seek(SeekFrom::Start(0)).expect("seek");
    file.write_all(&[0x5A; 512]).expect("a run writes its card");
    file.seek(SeekFrom::Start(32 << 20))
        .expect("seek into a hole");
    file.write_all(b"output_001.wav").expect("a run records");
    drop(file);

    assert_eq!(
        first_sector(&card),
        vec![0x5A; 512],
        "the run sees its own write"
    );
    assert!(
        fs::read(&template).expect("reread template") == before,
        "a run's writes reached the template, and with it every later run's card"
    );
}

#[test]
fn each_run_gets_its_own_copy() {
    let tmp = scratch("copy_per_run");
    let template = sparse_template(&tmp);

    let a = private_copy(&template).expect("first copy");
    let b = private_copy(&template).expect("second copy");
    assert_ne!(a, b, "two runs share one card path");
    open_like_the_card(&a)
        .write_all(&[0x11; 512])
        .expect("write a");

    assert_eq!(
        first_sector(&b),
        vec![0xA5; 512],
        "one run's write reached another's card"
    );
}

#[test]
fn a_copy_leaves_nothing_behind_to_clean_up() {
    let tmp = scratch("copy_unlinked");
    let template = sparse_template(&tmp);

    let _card = private_copy(&template).expect("copy");

    let names: Vec<_> = fs::read_dir(&tmp)
        .expect("list")
        .map(|e| e.expect("entry").file_name())
        .collect();
    assert_eq!(
        names,
        vec!["template.img"],
        "a run left a named copy behind"
    );
}

#[test]
fn a_copy_keeps_the_template_sparse() {
    let tmp = scratch("copy_sparse");
    let template = sparse_template(&tmp);

    let card = private_copy(&template).expect("copy");

    let meta = fs::metadata(&card).expect("stat the copy");
    assert_eq!(meta.len(), 64 << 20, "the copy has the template's length");
    assert!(
        meta.blocks() * 512 < 4 << 20,
        "the copy wrote out its holes: {} bytes on disk",
        meta.blocks() * 512
    );
}

#[test]
fn the_template_is_installed_read_only() {
    let tmp = scratch("template_read_only");

    let img =
        ensure_template(&tmp, "deadbeef", |staging| fs::write(staging, b"packed")).expect("pack");

    assert!(
        fs::metadata(&img).expect("stat").permissions().readonly(),
        "a writable template invites a run to mount it"
    );
}

#[test]
fn concurrent_runs_pack_the_template_exactly_once() {
    let tmp = scratch("template_pack_once");
    let packs = AtomicU32::new(0);

    let paths: Vec<PathBuf> = std::thread::scope(|scope| {
        let workers: Vec<_> = (0..8)
            .map(|_| {
                scope.spawn(|| {
                    ensure_template(&tmp, "deadbeef", |staging| {
                        packs.fetch_add(1, Ordering::SeqCst);
                        std::thread::sleep(std::time::Duration::from_millis(50));
                        fs::write(staging, b"packed")
                    })
                    .expect("ensure")
                })
            })
            .collect();
        workers
            .into_iter()
            .map(|w| w.join().expect("join"))
            .collect()
    });

    assert_eq!(
        packs.load(Ordering::SeqCst),
        1,
        "the template was packed more than once"
    );
    assert!(
        paths.iter().all(|p| *p == paths[0]),
        "runs disagree on the template: {paths:?}"
    );
}

#[test]
fn a_failed_pack_leaves_no_staging_file_or_template() {
    let tmp = scratch("template_pack_fails");

    let result = ensure_template(&tmp, "deadbeef", |staging| {
        fs::write(staging, b"half")?;
        Err(std::io::Error::other("mtools failed"))
    });

    assert!(result.is_err());
    let leftovers: Vec<_> = fs::read_dir(&tmp)
        .expect("list")
        .map(|e| e.expect("entry").file_name().to_string_lossy().into_owned())
        .filter(|n| !n.ends_with(".lock"))
        .collect();
    assert!(leftovers.is_empty(), "a failed pack left {leftovers:?}");
}

#[test]
fn a_dead_packers_staging_file_is_swept_before_packing() {
    let tmp = scratch("template_sweeps_staging");
    let dead = tmp.join("deluge-streaming-scenario-deadbeef.4294967295.part");
    fs::write(&dead, b"half-packed by a killed run").expect("leave a stale staging file");

    ensure_template(&tmp, "deadbeef", |staging| fs::write(staging, b"packed")).expect("pack");

    assert!(
        !dead.exists(),
        "a killed packer's staging file was left in the temp dir"
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
