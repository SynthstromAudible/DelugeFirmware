//! `embedded-fatfs` reads and writes over the FAT32 and FAT16 fixture images, checked against
//! known-good content: the committed `fixtures/tree/` files and the formulas
//! `fixtures/mk_fixture.sh` uses for its large synthetic files.
//!
//! The images come from `mk_fixture.sh`, via the `SP0_FAT32` and `SP0_FAT16` environment variables.
use fs_differential::diff::{compare_read, Captured, Node};
use fs_differential::ops::{Entry, FsOpsMut, Op};
use fs_differential::{efatfs::EFatFs, ram_disk::RamDisk};
use std::sync::Mutex;

/// Serializes tests over the process-wide [`RamDisk`] image; held for a test's whole
/// load-mount-read/write sequence.
static TEST_LOCK: Mutex<()> = Mutex::new(());

/// Reads a file from `fixtures/tree/`, which `mk_fixture.sh` copies unmodified onto the images.
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

/// `mk_fixture.sh`'s `huge.bin` (FAT32 only): 64 MiB, byte `i` is `(i >> 9) & 0xff`.
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

/// The fixture tree's known-good content. `has_huge` is true for FAT32; the FAT16 image is too
/// small for `huge.bin`.
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

#[test]
fn efatfs_reads_known_file_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let img = std::env::var("SP0_FAT32").expect("run mk_fixture.sh; set SP0_FAT32=/tmp/fat32.img");
    let _disk = RamDisk::load(&img);
    let fs = EFatFs::mount();
    assert_eq!(fs.read_file("/SAMPLES/hello.txt"), b"DELUGE-SP0\n");
}

/// The detach/reattach round trip the device handle table relies on (`File::close` →
/// `FileContext` → `File::new_from_context`) reads correct bytes, including with two handles'
/// reads interleaved.
///
/// Single-threaded: this covers the API round trip, not the device's mutex serialization.
#[test]
fn efatfs_context_roundtrip_and_interleave_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let img = std::env::var("SP0_FAT32").expect("run mk_fixture.sh; set SP0_FAT32=/tmp/fat32.img");
    let _disk = RamDisk::load(&img);
    let fs = EFatFs::mount();

    // Single-file round trip.
    let hello = fs.open_context("/SAMPLES/hello.txt");
    let mut buf = [0u8; 11]; // == len(b"DELUGE-SP0\n")
    let (hello, ok) = fs.read_at_context(&hello, 0, &mut buf);
    assert!(ok, "hello.txt fill was short");
    assert_eq!(
        &buf, b"DELUGE-SP0\n",
        "detach/reattach round-trip corrupted hello.txt"
    );

    // Two handles read round-robin; each reassembled file must match a whole-file read.
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

/// Compares the whole mounted tree of the image named by `env` with [`known_tree`].
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

/// The write corpus: mkdir, a multi-cluster LFN write, an extend across a cluster boundary, a
/// short-name write, a rename, and a delete of a fixture file.
fn write_corpus() -> Vec<Op> {
    vec![
        Op::Mkdir("/REC".into()),
        Op::Write("/REC/take 01.wav".into(), vec![0xAB; 40_000]), // multi-cluster, LFN name
        Op::Extend("/REC/take 01.wav".into(), vec![0xCD; 9_000]), // crosses a cluster boundary
        Op::Write("/REC/SHORT.RAW".into(), b"hi".to_vec()),
        Op::Rename("/REC/SHORT.RAW".into(), "/REC/Renamed Long.raw".into()),
        Op::Delete("/SAMPLES/hello.txt".into()),
    ]
}

/// Checks the state [`write_corpus`] should leave, through a fresh mount so it reads the disk
/// rather than the writer's cached state.
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

/// Raw FAT32 directory-entry parsing, to read a `..` entry that `read_dir` filters out.
mod raw_fat32 {
    /// The boot-sector fields needed to locate clusters (layout per Microsoft's `fatgen103.doc`).
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

    /// Returns the byte offset of the entry named `sfn11` (11-byte space-padded short name) in the
    /// directory at `dir_off`, skipping deleted (0xE5) and LFN (attr 0x0F) entries.
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

    /// Returns the first-cluster field (high word at 0x14, low at 0x1A) of the entry at
    /// `entry_off`.
    fn entry_first_cluster(img: &[u8], entry_off: usize) -> u32 {
        let hi = u16::from_le_bytes([img[entry_off + 0x14], img[entry_off + 0x15]]);
        let lo = u16::from_le_bytes([img[entry_off + 0x1A], img[entry_off + 0x1B]]);
        ((hi as u32) << 16) | lo as u32
    }

    /// For a directory `dirname_sfn11` directly under the root, returns its first cluster and the
    /// first-cluster field of its `..` entry.
    pub fn probe_dotdot_cluster(img: &[u8], dirname_sfn11: &[u8; 11]) -> (u32, u32) {
        let bpb = parse_bpb32(img);
        let root_off = cluster_offset(&bpb, bpb.root_cluster);
        let dir_entry_off = find_sfn_entry(img, root_off, dirname_sfn11)
            .expect("directory entry not found under root");
        let dir_cluster = entry_first_cluster(img, dir_entry_off);
        let dir_off = cluster_offset(&bpb, dir_cluster);
        // `create_dir` writes "." then "..".
        let dotdot_off = dir_off + 32;
        assert_eq!(
            &img[dotdot_off..dotdot_off + 2],
            b"..",
            "expected '..' entry at index 1"
        );
        (dir_cluster, entry_first_cluster(img, dotdot_off))
    }
}

/// A directory created under the FAT32 root gets 0 in its `..` entry's cluster field, as the FAT
/// spec requires, not the root's cluster number. Guards the vendored `create_dir` fix (see
/// `crates/embedded-fatfs/VENDOR.md`).
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

    assert_eq!(
        e_dotdot_cluster, 0,
        "embedded-fatfs should write 0 into '..' under root (BUG-B, Task 6B)"
    );
}

/// A card swap: after mounting FAT32, replacing the image with FAT16 and remounting reads the new
/// image, so no mount state carries over. Card detect and controller re-init are device-only and
/// not covered.
#[test]
fn reinsert_second_image_remount_and_op_succeeds() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());

    let fat32_path =
        std::env::var("SP0_FAT32").expect("run mk_fixture.sh; set SP0_FAT32=/tmp/fat32.img");
    RamDisk::load(&fat32_path);
    let e1 = EFatFs::mount();
    assert_eq!(
        e1.read_file("SAMPLES/hello.txt"),
        tree_file("SAMPLES/hello.txt"),
        "op on the first image (pre-swap) must succeed and read the first image's content"
    );
    drop(e1); // Nothing survives the swap; files are identified by path.

    // Swap in an image with a different FAT type, size and cluster size.
    let fat16_path =
        std::env::var("SP0_FAT16").expect("run mk_fixture.sh; set SP0_FAT16=/tmp/fat16.img");
    RamDisk::load(&fat16_path);

    let e2 = EFatFs::mount();
    assert_eq!(
        e2.read_file("SAMPLES/hello.txt"),
        tree_file("SAMPLES/hello.txt"),
        "op after the swap must succeed and read the SECOND image's content"
    );
    // Only the FAT32 image has huge.bin, so this proves the FAT16 tree is being read.
    assert!(
        !e2.exists("SAMPLES/huge.bin"),
        "post-swap mount must reflect the SECOND image's own directory tree, \
         not a leftover view of the first image"
    );
}
