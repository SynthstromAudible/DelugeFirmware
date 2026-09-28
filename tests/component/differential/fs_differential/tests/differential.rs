//! SP0 harness integration tests: `embedded-fatfs`, driven over the shared
//! `DISK` RAM image, checked against known-good expected bytes/listings
//! (the fixture tree's own committed source files, plus the deterministic
//! generator formulas `fixtures/mk_fixture.sh` uses for its two large
//! synthetic files).
use fs_differential::diff::{compare_read, Captured, Node};
use fs_differential::ops::{Entry, FsOpsMut, Op};
use fs_differential::{efatfs::EFatFs, ram_disk::RamDisk};
use std::sync::Mutex;

/// `DISK` (`ram_disk.rs`) is a process-wide singleton. `cargo test` runs
/// `#[test]`s in parallel threads by default, so any two tests that load an
/// image / mount a filesystem would race on that shared state. Every such
/// test takes this lock first, for the duration of the whole
/// load-mount-read(/write) sequence, so only one is ever touching the
/// shared image at a time -- an alternative to `--test-threads=1` that
/// doesn't serialize the whole binary.
static TEST_LOCK: Mutex<()> = Mutex::new(());

/// Reads a fixture-tree source file's REAL bytes straight from
/// `fixtures/tree/` -- the same files `fixtures/mk_fixture.sh` copies
/// unmodified onto the card images -- so comparisons below are against the
/// actual committed source of truth, not a hand-copied literal that could
/// drift from it.
fn tree_file(rel: &str) -> Vec<u8> {
    let path = format!(
        concat!(env!("CARGO_MANIFEST_DIR"), "/fixtures/tree/{}"),
        rel
    );
    std::fs::read(&path).unwrap_or_else(|e| panic!("read fixture tree file {path}: {e}"))
}

/// `mk_fixture.sh`'s `big_multicluster.bin`: 1 MiB, every byte `0x55`.
fn known_big_multicluster() -> Vec<u8> {
    vec![0x55u8; 1_048_576]
}

/// `mk_fixture.sh`'s `huge.bin` (FAT32 fixture only): 64 MiB, byte at offset
/// `i` is `(i >> 9) & 0xff` -- the exact formula the fixture generator's
/// Python one-liner uses.
fn known_huge() -> Vec<u8> {
    (0..67_108_864u64)
        .map(|i| ((i >> 9) & 0xff) as u8)
        .collect()
}

fn dir_entry(name: &str) -> Entry {
    Entry {
        name: name.to_string(),
        size: 0,
        is_dir: true,
    }
}

fn file_entry(name: &str, bytes: &[u8]) -> Entry {
    Entry {
        name: name.to_string(),
        size: bytes.len() as u64,
        is_dir: false,
    }
}

/// The whole fixture tree's known-good expected content, as a literal
/// [`Node`] tree `compare_read` can diff a live `EFatFs` mount against.
/// `has_huge` selects FAT32 (has `huge.bin`) vs FAT16 (doesn't -- see
/// `mk_fixture.sh`'s comment on why: the FAT16 image can't hold a 64 MiB
/// file plus the rest of the tree).
fn known_tree(has_huge: bool) -> Captured {
    let kick_bytes = tree_file("SAMPLES/Kicks/Deep House Kick (loud).wav");
    let hello_bytes = tree_file("SAMPLES/hello.txt");
    let song1_bytes = tree_file("SONGS/My Long Song Name 01.XML");
    let song0_bytes = tree_file("SONGS/SONG000.XML");

    let mut samples_kids = vec![
        (
            dir_entry("Kicks"),
            Node::Dir(vec![(
                file_entry("Deep House Kick (loud).wav", &kick_bytes),
                Node::File(kick_bytes),
            )]),
        ),
        (
            file_entry("big_multicluster.bin", &known_big_multicluster()),
            Node::File(known_big_multicluster()),
        ),
        (
            file_entry("hello.txt", &hello_bytes),
            Node::File(hello_bytes),
        ),
    ];
    if has_huge {
        let huge = known_huge();
        samples_kids.push((file_entry("huge.bin", &huge), Node::File(huge)));
    }
    samples_kids.sort_by(|a, b| a.0.name.cmp(&b.0.name));

    let mut songs_kids = vec![
        (
            file_entry("My Long Song Name 01.XML", &song1_bytes),
            Node::File(song1_bytes),
        ),
        (
            file_entry("SONG000.XML", &song0_bytes),
            Node::File(song0_bytes),
        ),
    ];
    songs_kids.sort_by(|a, b| a.0.name.cmp(&b.0.name));

    let mut root_kids = vec![
        (dir_entry("SAMPLES"), Node::Dir(samples_kids)),
        (dir_entry("SONGS"), Node::Dir(songs_kids)),
    ];
    root_kids.sort_by(|a, b| a.0.name.cmp(&b.0.name));

    Captured::literal(Node::Dir(root_kids))
}

/// Drives the vendored `embedded-fatfs` against a FAT32 card image built by
/// `fixtures/mk_fixture.sh`, and checks it reads back a known fixture file
/// byte-for-byte.
#[test]
fn efatfs_reads_known_file_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let img = std::env::var("SP0_FAT32").expect("run mk_fixture.sh; set SP0_FAT32=/tmp/fat32.img");
    let _disk = RamDisk::load(&img);
    let fs = EFatFs::mount();
    assert_eq!(fs.read_file("/SAMPLES/hello.txt"), b"DELUGE-SP0\n");
}

/// Host analog: proves the `embedded-fatfs` detach/reattach
/// (`File::close` → [`FileContext`](embedded_fatfs::FileContext) →
/// `File::new_from_context`) round-trip the device handle table
/// (`src/efatfs_fs.rs`) relies on reads back correct bytes — including when
/// two reconstructed handles' reads are interleaved round-robin (the on-host
/// analog of the device mutex serializing concurrent streamed reads).
///
/// DIVERGENCE: host is single-threaded `block_on`, so this validates the
/// embedded-fatfs API round-trip + interleave correctness ONLY, NOT the device
/// `static`/embassy-`Mutex` serialization — that still needs on-device
/// verification.
#[test]
fn efatfs_context_roundtrip_and_interleave_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let img = std::env::var("SP0_FAT32").expect("run mk_fixture.sh; set SP0_FAT32=/tmp/fat32.img");
    let _disk = RamDisk::load(&img);
    let fs = EFatFs::mount();

    // (a) single-file detach → reattach → seek(0) → fill round-trip.
    let hello = fs.open_context("/SAMPLES/hello.txt");
    let mut buf = [0u8; 11]; // == len(b"DELUGE-SP0\n")
    let (hello, ok) = fs.read_at_context(&hello, 0, &mut buf);
    assert!(ok, "hello.txt fill was short");
    assert_eq!(
        &buf, b"DELUGE-SP0\n",
        "detach/reattach round-trip corrupted hello.txt"
    );

    // (b) two handles; reads interleaved round-robin across both reconstructed
    //     contexts. Each file's bytes, reassembled from its chunks, must match
    //     the whole-file oracle — proving detach/reattach doesn't corrupt one
    //     handle's stream when another's reads are interleaved with it.
    let kick_path = "/SAMPLES/Kicks/Deep House Kick (loud).wav";
    let expected_hello = fs.read_file("/SAMPLES/hello.txt");
    let expected_kick = fs.read_file(kick_path);

    let mut h_hello = hello; // reuse the already-advanced context
    let mut h_kick = fs.open_context(kick_path);
    let (mut got_hello, mut got_kick) = (Vec::new(), Vec::new());
    let (mut off_hello, mut off_kick) = (0u32, 0u32);
    const CHUNK: usize = 8; // small, so both short files yield several interleaved reads

    loop {
        let mut progressed = false;
        for (path_ctx, off, expected, got) in [
            (
                &mut h_hello,
                &mut off_hello,
                &expected_hello,
                &mut got_hello,
            ),
            (&mut h_kick, &mut off_kick, &expected_kick, &mut got_kick),
        ] {
            let remaining = expected.len() - *off as usize;
            if remaining == 0 {
                continue;
            }
            let want = CHUNK.min(remaining);
            let mut chunk = vec![0u8; want];
            let (advanced, ok) = fs.read_at_context(path_ctx, *off, &mut chunk);
            assert!(ok, "interleaved chunk at offset {off} was short");
            *path_ctx = advanced;
            got.extend_from_slice(&chunk);
            *off += want as u32;
            progressed = true;
        }
        if !progressed {
            break;
        }
    }
    assert_eq!(got_hello, expected_hello, "interleave corrupted hello.txt");
    assert_eq!(
        got_kick, expected_kick,
        "interleaved reads corrupted the Kicks wav"
    );
}

/// Walks the WHOLE fixture tree and asserts efatfs agrees with the
/// known-good expected content at every level -- SP0's core instrument, run
/// against the FAT32 image.
fn run_read_check(env: &str, has_huge: bool) {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let img = std::env::var(env)
        .unwrap_or_else(|_| panic!("run mk_fixture.sh; set {env}=/tmp/<variant>.img"));
    let _disk = RamDisk::load(&img);
    let e = EFatFs::mount();
    compare_read(&e, &known_tree(has_huge)).expect("read/enumerate against known-good tree");
}

#[test]
fn read_diff_fat32() {
    run_read_check("SP0_FAT32", true);
}

#[test]
fn read_diff_fat16() {
    run_read_check("SP0_FAT16", false);
}

/// The write-path corpus: mkdir, a multi-cluster LFN-named write,
/// an extend that grows the same file further, a short-name write, a
/// rename, and a delete of a pre-existing fixture file. Exercises create,
/// extend-across-cluster-boundary, LFN entry creation, rename, and delete
/// in one pass, on both FAT variants.
fn write_corpus() -> Vec<Op> {
    vec![
        Op::Mkdir("/REC".into()),
        Op::Write("/REC/take 01.wav".into(), vec![0xAB; 40_000]), // multi-cluster, LFN name
        Op::Extend("/REC/take 01.wav".into(), vec![0xCD; 9_000]), // extend, further multi-cluster growth
        Op::Write("/REC/SHORT.RAW".into(), b"hi".to_vec()),
        Op::Rename("/REC/SHORT.RAW".into(), "/REC/Renamed Long.raw".into()),
        Op::Delete("/SAMPLES/hello.txt".into()),
    ]
}

/// The expected post-`write_corpus()` state, computed directly from the
/// corpus's own literal payloads (fully known ahead of time -- no oracle
/// needed). Checked via a FRESH [`EFatFs::mount`] so the check walks the
/// on-disk directory/FAT structures from scratch, not any in-process state
/// the writer's `EFatFs` instance cached.
fn assert_write_corpus_result() {
    let e = EFatFs::mount();

    let mut want_take01 = vec![0xABu8; 40_000];
    want_take01.extend(std::iter::repeat_n(0xCDu8, 9_000));
    assert_eq!(
        e.read_file("/REC/take 01.wav"),
        want_take01,
        "extend-across-cluster-boundary result diverged"
    );

    assert_eq!(
        e.read_file("/REC/Renamed Long.raw"),
        b"hi",
        "renamed file's content diverged"
    );
    assert!(
        !e.exists("/REC/SHORT.RAW"),
        "rename must remove the old name"
    );
    assert!(
        !e.exists("/SAMPLES/hello.txt"),
        "delete must remove hello.txt"
    );
}

/// Replays `write_corpus()` against the FAT32 image and checks the result
/// against the corpus's own known-good expectation -- SP0's write-path
/// instrument.
#[test]
fn write_corpus_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let img = std::env::var("SP0_FAT32").expect("run mk_fixture.sh; set SP0_FAT32=/tmp/fat32.img");
    let _disk = RamDisk::load(&img);
    let mut e = EFatFs::mount();
    for op in write_corpus() {
        e.apply(&op);
    }
    assert_write_corpus_result();
}

/// Same corpus, FAT16 image.
#[test]
fn write_corpus_fat16() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let img = std::env::var("SP0_FAT16").expect("run mk_fixture.sh; set SP0_FAT16=/tmp/fat16.img");
    let _disk = RamDisk::load(&img);
    let mut e = EFatFs::mount();
    for op in write_corpus() {
        e.apply(&op);
    }
    assert_write_corpus_result();
}

/// FAT32-only raw-entry parsing helper for the `..` demonstration probe
/// below. efatfs's own `read_dir` filters `.`/`..` out (see `efatfs.rs`'s
/// `read_dir` doc) -- so the only way to see what it actually wrote into a
/// `..` entry's first-cluster field is to read the raw 32-byte directory
/// entry bytes straight out of the image.
mod raw_fat32 {
    /// The BPB fields needed to locate a cluster's first byte and a
    /// directory's entries within a raw FAT32 image (boot-sector layout;
    /// see e.g. Microsoft's `fatgen103.doc`).
    struct Bpb32 {
        bytes_per_sector: u32,
        sectors_per_cluster: u32,
        first_data_sector: u32,
        root_cluster: u32,
    }

    fn parse_bpb32(img: &[u8]) -> Bpb32 {
        let bytes_per_sector = u16::from_le_bytes([img[11], img[12]]) as u32;
        let sectors_per_cluster = img[13] as u32;
        let reserved_sectors = u16::from_le_bytes([img[14], img[15]]) as u32;
        let num_fats = img[16] as u32;
        let fat_size_32 = u32::from_le_bytes([img[36], img[37], img[38], img[39]]);
        let root_cluster = u32::from_le_bytes([img[44], img[45], img[46], img[47]]);
        let first_data_sector = reserved_sectors + num_fats * fat_size_32;
        Bpb32 {
            bytes_per_sector,
            sectors_per_cluster,
            first_data_sector,
            root_cluster,
        }
    }

    fn cluster_offset(bpb: &Bpb32, cluster: u32) -> usize {
        let sector = bpb.first_data_sector + (cluster - 2) * bpb.sectors_per_cluster;
        sector as usize * bpb.bytes_per_sector as usize
    }

    /// Scans 32-byte directory entries starting at byte offset `dir_off` for
    /// a short-name (11-byte, space-padded) match, skipping deleted (0xE5)
    /// and LFN (attr 0x0F) entries. Returns the matching entry's own byte
    /// offset.
    fn find_sfn_entry(img: &[u8], dir_off: usize, sfn11: &[u8; 11]) -> Option<usize> {
        let mut off = dir_off;
        loop {
            if img[off] == 0x00 {
                return None; // end of directory
            }
            let attr = img[off + 11];
            if img[off] != 0xE5 && attr != 0x0F && &img[off..off + 11] == sfn11 {
                return Some(off);
            }
            off += 32;
        }
    }

    /// The first-cluster field (hi word at 0x14, lo word at 0x1A) of the
    /// 32-byte directory entry starting at `entry_off`.
    fn entry_first_cluster(img: &[u8], entry_off: usize) -> u32 {
        let hi = u16::from_le_bytes([img[entry_off + 0x14], img[entry_off + 0x15]]);
        let lo = u16::from_le_bytes([img[entry_off + 0x1A], img[entry_off + 0x1B]]);
        ((hi as u32) << 16) | lo as u32
    }

    /// For a FAT32 image that has had a directory named `dirname` (an 8.3
    /// short name, e.g. `"REC        "`, 11 bytes space-padded) created
    /// directly under the root: returns `(dir's own first cluster, the
    /// first-cluster field written into that dir's own ".." entry)`.
    pub fn probe_dotdot_cluster(img: &[u8], dirname_sfn11: &[u8; 11]) -> (u32, u32) {
        let bpb = parse_bpb32(img);
        let root_off = cluster_offset(&bpb, bpb.root_cluster);
        let dir_entry_off = find_sfn_entry(img, root_off, dirname_sfn11)
            .expect("directory entry not found under root");
        let dir_cluster = entry_first_cluster(img, dir_entry_off);
        let dir_off = cluster_offset(&bpb, dir_cluster);
        // "." is entry index 0, ".." is entry index 1 in a freshly-created
        // directory (embedded-fatfs's `create_dir` writes them in that
        // order -- see dir.rs).
        let dotdot_off = dir_off + 32;
        assert_eq!(
            &img[dotdot_off..dotdot_off + 2],
            b"..",
            "expected '..' entry at index 1"
        );
        (dir_cluster, entry_first_cluster(img, dotdot_off))
    }
}

/// Regression test: embedded-fatfs's `Dir::create_dir` used to write the
/// ROOT's own first cluster into a new directory's `..` entry when that
/// directory is created directly under the FAT32 root, instead of the FAT
/// convention (0, meaning "parent is the root"). The fix ports upstream
/// rust-fatfs's `c4bb769` into `crates/embedded-fatfs/src/dir.rs`'s
/// `create_dir` (an `is_root_dir()` distinction on `DirRawStream`/`File`).
/// This probe reads the raw on-disk `..` entry directly, bypassing
/// `read_dir` (which filters `.`/`..` out -- see `raw_fat32` module doc).
#[test]
fn fat32_dotdot_cluster_probe() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let img_path =
        std::env::var("SP0_FAT32").expect("run mk_fixture.sh; set SP0_FAT32=/tmp/fat32.img");
    let orig = std::fs::read(&img_path).expect("read fixture image");
    let rec_sfn: [u8; 11] = *b"REC        ";

    let disk = RamDisk::load_bytes(&orig);
    let e = EFatFs::mount();
    e.mkdir("/REC");
    drop(e);
    let e_image = disk.snapshot();
    let (e_rec_cluster, e_dotdot_cluster) = raw_fat32::probe_dotdot_cluster(&e_image, &rec_sfn);

    eprintln!("FAT32 '..' probe: embedded-fatfs -- REC cluster={e_rec_cluster}, '..' cluster field={e_dotdot_cluster}");

    // FAT spec convention: a directory whose parent is the root writes 0
    // into its ".." entry, regardless of the root's own actual
    // first-cluster number.
    assert_eq!(
        e_dotdot_cluster, 0,
        "embedded-fatfs should write 0 into '..' under root (BUG-B, Task 6B)"
    );
}

// --- Card-reinsert / swap proxy ---------------------------------------------
//
// R4 Phase C Task 8's gate asks for a "mount -> op -> remount against a
// second image -> op succeeds" scenario, to the extent the host shim can
// model a card swap. The host has no card-detect line (that's a BSP/hardware
// concept -- `deluge_block_ready`, per the project-model notes -- and is
// untestable here), but `RamDisk::load_bytes` IS the host-level equivalent of
// "different physical bytes are now behind the block device": it replaces
// the whole backing image `FileBlockDevice`/`BufStream` read/write through,
// exactly as a real card swap replaces what's behind the SD controller. This
// proves `efatfs_core`'s open/mount path has no cross-mount state (cached FAT
// sectors, directory cursors, a stale generation) that would corrupt or wedge
// against a SECOND, geometrically-different image (FAT32 -> FAT16: different
// FAT type, different total size, different cluster size) mounted right after
// the first. The genuinely device-only part -- the actual electrical
// card-detect transition and re-initializing the SPI/SD controller -- is
// deferred to the on-device ear-check (see the R4 Phase C gate report).
#[test]
fn reinsert_second_image_remount_and_op_succeeds() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());

    // Mount the FIRST image (FAT32) and do a real op: read a known file.
    let fat32_path =
        std::env::var("SP0_FAT32").expect("run mk_fixture.sh; set SP0_FAT32=/tmp/fat32.img");
    RamDisk::load(&fat32_path);
    let e1 = EFatFs::mount();
    assert_eq!(
        e1.read_file("SAMPLES/hello.txt"),
        tree_file("SAMPLES/hello.txt"),
        "op on the first image (pre-swap) must succeed and read the first image's content"
    );
    drop(e1); // no held locator survives the swap -- UI/storage identify files by path.

    // "Reinsert": the backing bytes are swapped out from under the block
    // device for a SECOND, geometrically-different image (FAT16, not FAT32)
    // -- `RamDisk::load` replaces `DISK` wholesale, exactly as a real card
    // swap replaces what a fresh `f_mount`-equivalent open would read.
    let fat16_path =
        std::env::var("SP0_FAT16").expect("run mk_fixture.sh; set SP0_FAT16=/tmp/fat16.img");
    RamDisk::load(&fat16_path);

    // A fresh mount + op against the NEW image must succeed and see the
    // SECOND image's content -- not wedge, not silently keep serving the
    // first image's stale directory/FAT state.
    let e2 = EFatFs::mount();
    assert_eq!(
        e2.read_file("SAMPLES/hello.txt"),
        tree_file("SAMPLES/hello.txt"),
        "op after the swap must succeed and read the SECOND image's content"
    );
    // The FAT16 fixture has no huge.bin (mk_fixture.sh: FAT16 image can't
    // hold it) -- confirms this really is walking the FAT16 image's own
    // directory structure, not a cached/stale FAT32 view.
    assert!(
        !e2.exists("SAMPLES/huge.bin"),
        "post-swap mount must reflect the SECOND image's own directory tree, \
         not a leftover view of the first image"
    );
}
