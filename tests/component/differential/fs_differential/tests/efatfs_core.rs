//! Host coverage of the REAL `efatfs_core` handle-table logic.
//!
//! The device handle table (`src/efatfs_fs.rs`) is `#![cfg(target_os = "none")]`
//! and can't be reached from any host build; its logic lives in the
//! storage-generic `src/efatfs_core.rs`. This test `#[path]`-includes that
//! exact source and drives it over `fs_differential`'s host `FileSystem`
//! (`FileBlockDevice` → `BufStream` → embedded-fatfs), so the generation guard,
//! the `FileContext` detach/reattach, and the fill loop get real execution on
//! host.
//!
//! DIVERGENCE: host is single-threaded `block_on`, so this validates the core's
//! logic (round-trip, interleave, recycle guard) ONLY, not the device
//! `static`/embassy-`Mutex` serialization — that still needs on-device
//! verification.

// Recompile the real core into this test binary (same `#[path]` convention the
// BSP host tests use). efatfs_core depends only on embedded_fatfs +
// embedded_io_async, both of which are fs_differential deps.
#[path = "../../../../../src/bsp/rust/src/efatfs_core.rs"]
mod efatfs_core;

// `efatfs_core.rs`'s `use crate::sys::{DelugeStatus, ...}` expects the real
// libdeluge bindgen output the BSP crate provides (`target_os = "none"` or
// `feature = "host_app"`) -- this test binary is a standalone crate with
// neither, so it needs its own stand-in, the same values
// `include/libdeluge/types.h`'s `DelugeStatus` pins.
#[allow(non_upper_case_globals)]
mod sys {
    pub type DelugeStatus = i8;
    pub const DelugeStatus_DELUGE_ERR_PARAM: DelugeStatus = -2;
    pub const DelugeStatus_DELUGE_ERR_IO: DelugeStatus = -5;
    pub const DelugeStatus_DELUGE_ERR_NOT_FOUND: DelugeStatus = -8;
    pub const DelugeStatus_DELUGE_ERR_EXISTS: DelugeStatus = -9;
    pub const DelugeStatus_DELUGE_ERR_NO_SPACE: DelugeStatus = -10;
    pub const DelugeStatus_DELUGE_ERR_NO_FILESYSTEM: DelugeStatus = -11;
    pub const DelugeStatus_DELUGE_ERR_NOT_EMPTY: DelugeStatus = -14;
}

use aligned::{Aligned, A4};
use block_device_adapters::BufStream;
use block_device_driver::BlockDevice;
use efatfs_core::HandleTable;
use embassy_futures::block_on;
use embedded_fatfs::{DefaultTimeProvider, FileSystem, FsOptions, LossyOemCpConverter};
use fs_differential::{efatfs::EFatFs, ram_disk::RamDisk};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

// Shared with tests/differential.rs's rationale: the RAM `DISK` is a
// process-wide singleton, so serialize the load-mount-read span.
static TEST_LOCK: Mutex<()> = Mutex::new(());

fn fat32() -> String {
    std::env::var("SP0_FAT32").expect("run mk_fixture.sh; set SP0_FAT32=/tmp/fat32.img")
}

/// `mk_fixture.sh`'s `/SAMPLES/huge.bin` (FAT32 fixture only): 64 MiB, byte
/// at offset `i` is `(i >> 9) & 0xff` -- the exact formula the fixture
/// generator's Python one-liner uses. A deterministic known-good "oracle"
/// that doesn't require a second filesystem implementation to compute.
fn known_huge_bytes() -> Vec<u8> {
    (0..67_108_864u64)
        .map(|i| ((i >> 9) & 0xff) as u8)
        .collect()
}

/// (a) round-trip + (b) two-handle interleave, driven through the REAL
/// `efatfs_core::{open_context, HandleTable::read_at_owned}` — the same code
/// the device wraps, not a parallel reimplementation.
#[test]
fn efatfs_core_open_read_and_interleave_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let efatfs = EFatFs::mount();
    let fs = efatfs.raw();
    let mut table = HandleTable::<{ efatfs_core::MAX_HANDLES }>::new();

    block_on(async {
        // (a) open → insert → read_at_owned(0) → whole-file bytes.
        let ctx = efatfs_core::open_context(fs, "/SAMPLES/hello.txt")
            .await
            .expect("open_context hello.txt");
        let h = table.insert(ctx).expect("table not full");
        let mut buf = [0u8; 11]; // len(b"DELUGE-SP0\n")
        assert!(
            table.read_at_owned(fs, h, 0, &mut buf).await,
            "hello.txt fill was short"
        );
        assert_eq!(
            &buf, b"DELUGE-SP0\n",
            "core detach/reattach corrupted hello.txt"
        );

        // (b) two handles, reads interleaved round-robin, reassembled bytes must
        //     match the whole-file oracle for each.
        let kick_path = "/SAMPLES/Kicks/Deep House Kick (loud).wav";
        let expected_hello = efatfs.read_file("/SAMPLES/hello.txt");
        let expected_kick = efatfs.read_file(kick_path);

        let kctx = efatfs_core::open_context(fs, kick_path)
            .await
            .expect("open_context kick");
        let hk = table.insert(kctx).expect("table not full");

        let (mut got_hello, mut got_kick) = (Vec::new(), Vec::new());
        let (mut off_hello, mut off_kick) = (0u32, 0u32);
        const CHUNK: usize = 8;
        loop {
            let mut progressed = false;
            for (handle, off, expected, got) in [
                (h, &mut off_hello, &expected_hello, &mut got_hello),
                (hk, &mut off_kick, &expected_kick, &mut got_kick),
            ] {
                let remaining = expected.len() - *off as usize;
                if remaining == 0 {
                    continue;
                }
                let want = CHUNK.min(remaining);
                let mut chunk = vec![0u8; want];
                assert!(
                    table.read_at_owned(fs, handle, *off, &mut chunk).await,
                    "interleaved chunk at offset {off} was short"
                );
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
            "interleave corrupted the Kicks wav"
        );
    });
}

/// The generation guard: a `checkout` captured before a `remove` + `insert`
/// recycle of the same slot index must NOT be able to `commit` a stale context
/// onto the file now occupying that slot. This is the device `read_at`
/// write-back race the generation guard protects against — untestable on the
/// device (statics), covered here against the real core.
#[test]
fn efatfs_core_generation_guard_rejects_stale_commit() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let efatfs = EFatFs::mount();
    let fs = efatfs.raw();
    let mut table = HandleTable::<{ efatfs_core::MAX_HANDLES }>::new();

    block_on(async {
        let kick_path = "/SAMPLES/Kicks/Deep House Kick (loud).wav";

        // Occupy a slot with hello.txt, then capture a checkout (handle + gen)
        // and the hello context to try to write back later.
        let hello_ctx = efatfs_core::open_context(fs, "/SAMPLES/hello.txt")
            .await
            .expect("open hello");
        let h = table.insert(hello_ctx).expect("insert hello");
        let (stale_gen, stale_ctx) = table.checkout(h).expect("checkout hello");

        // Recycle that exact slot index for a DIFFERENT file (kick): remove
        // bumps the generation, insert reuses the lowest-free index (== h).
        table.remove(h);
        let kick_ctx = efatfs_core::open_context(fs, kick_path)
            .await
            .expect("open kick");
        let h2 = table.insert(kick_ctx).expect("insert kick");
        assert_eq!(h2, h, "insert should reuse the freed lowest index");

        // The stale write-back must be rejected (generation moved on).
        table.commit(h, stale_gen, stale_ctx);

        // Reading the recycled handle must yield KICK bytes, not corrupted /
        // hello data — proving the stale commit did not splice hello's context
        // onto the kick slot.
        let expected_kick = efatfs.read_file(kick_path);
        let mut buf = vec![0u8; 16.min(expected_kick.len())];
        assert!(
            table.read_at_owned(fs, h, 0, &mut buf).await,
            "kick read after recycle was short"
        );
        assert_eq!(
            &buf[..],
            &expected_kick[..buf.len()],
            "stale commit corrupted the recycled slot's file identity"
        );
    });
}

// --- Lens-1 margin proxy: SD-transfer overhead of an efatfs cluster read -----
//
// The Lens-1 concern is that embedded-fatfs's per-read cost (FAT-chain walk +
// single-block `BufStream`) is heavier than the raw cluster→sector-map read the
// C-FatFS path does (one contiguous DMA of the cluster's data sectors). The
// fully-integrated modeled-latency margin (the real C++ streaming scenario in
// `lens1_vt_sim`) is deferred; this is the lightweight direct proxy: count the
// 512-byte block reads embedded-fatfs issues to sequentially read a multi-
// cluster file, and compare against the ideal (one data read per data sector,
// no FAT-walk/re-read overhead). A pathological O(n) chain re-walk on every
// read — the spec's stated risk — would blow this number up.

/// Total 512-byte blocks read since the last reset (process-wide; serialized by
/// `TEST_LOCK`).
static BLOCKS_READ: AtomicU64 = AtomicU64::new(0);

/// `BlockDevice<512>` that counts blocks read, delegating to the shared `DISK`
/// exactly as `FileBlockDevice` does.
struct CountingBlockDevice;

impl BlockDevice<512> for CountingBlockDevice {
    type Align = A4;
    type Error = core::convert::Infallible;

    async fn read(
        &mut self,
        block_address: u32,
        data: &mut [Aligned<A4, [u8; 512]>],
    ) -> Result<(), Self::Error> {
        BLOCKS_READ.fetch_add(data.len() as u64, Ordering::Relaxed);
        for (i, blk) in data.iter_mut().enumerate() {
            let off = (block_address as u64 + i as u64) * 512;
            RamDisk::read_at(off, &mut blk[..]);
        }
        Ok(())
    }

    async fn write(
        &mut self,
        block_address: u32,
        data: &[Aligned<A4, [u8; 512]>],
    ) -> Result<(), Self::Error> {
        for (i, blk) in data.iter().enumerate() {
            let off = (block_address as u64 + i as u64) * 512;
            RamDisk::write_at(off, &blk[..]);
        }
        Ok(())
    }

    async fn size(&mut self) -> Result<u64, Self::Error> {
        Ok(RamDisk::len())
    }
}

#[test]
fn efatfs_core_sequential_read_transfer_overhead() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());

    // The mk_fixture.sh FAT32 image is formatted `-c 64` → 64 sectors (32 KiB)
    // per cluster; that's the raw-map path's contiguous read size.
    const CLUSTER_SECTORS: u64 = 64;
    let kick_path = "/SAMPLES/big_multicluster.bin"; // 1 MiB = 32 clusters

    // Whole-file size via the ordinary EFatFs oracle (uncounted).
    let file_len = EFatFs::mount().read_file(kick_path).len() as u64;
    let data_sectors = file_len.div_ceil(512); // ideal: one read per data sector
    let clusters = file_len.div_ceil(CLUSTER_SECTORS * 512);

    // Mount a fresh FS over the counting device and read the file sequentially,
    // one cluster (32 KiB) at a time, through the real efatfs_core.
    let storage = BufStream::<CountingBlockDevice, 512>::new(CountingBlockDevice);
    let fs: FileSystem<_, DefaultTimeProvider, LossyOemCpConverter> =
        block_on(FileSystem::new(storage, FsOptions::new())).expect("mount counting FS");

    block_on(async {
        let ctx = efatfs_core::open_context(&fs, kick_path)
            .await
            .expect("open kick");
        let mut table = HandleTable::<{ efatfs_core::MAX_HANDLES }>::new();
        let h = table.insert(ctx).expect("insert");

        BLOCKS_READ.store(0, Ordering::Relaxed); // count only the streamed reads
        let cluster_bytes = (CLUSTER_SECTORS * 512) as usize;
        let mut off: u32 = 0;
        while (off as u64) < file_len {
            let want = cluster_bytes.min((file_len - off as u64) as usize);
            let mut buf = vec![0u8; want];
            assert!(
                table.read_at_owned(&fs, h, off, &mut buf).await,
                "cluster read at {off} was short"
            );
            off += want as u32;
        }
    });

    let blocks = BLOCKS_READ.load(Ordering::Relaxed);
    let overhead = blocks as f64 / data_sectors as f64;
    println!(
        "lens1-proxy: file={file_len}B clusters={clusters} data_sectors={data_sectors} \
         efatfs_block_reads={blocks} overhead={overhead:.2}x (ideal 1.00x = raw-map data-only)"
    );

    // Sequential reads should stay near the ideal — a few extra reads per
    // cluster for the FAT-chain walk, NOT an O(n) re-walk of the whole chain on
    // every cluster (which would scale the overhead with `clusters`). Guard
    // generously (3x) so this catches a pathological regression, not tuning
    // noise.
    assert!(
        overhead < 3.0,
        "efatfs sequential-read transfer overhead {overhead:.2}x too high \
         (blocks={blocks}, data_sectors={data_sectors}, clusters={clusters}) — \
         possible O(n) FAT-chain re-walk regression"
    );
}

/// Looping-playback proxy: models a sample looping over its back half — one
/// BACKWARD seek at the loop point, then ~1024 clusters read forward
/// SEQUENTIALLY before the next wrap. `seek()` has no "continue from
/// current_cluster" branch (file.rs:489-516), so the one seek per wrap does
/// re-walk the chain from `first_cluster`, but that cost is amortized over
/// the ~1024 sequential reads that follow it — each of those hits `seek()`'s
/// "already at this offset" early return (file.rs:485) and costs nothing
/// extra. Four wraps here spend one re-walk each against 4096 total reads.
///
/// IMPORTANT: this measures the cost real looping playback actually pays —
/// it does NOT measure seek cost in isolation, and its low overhead is NOT
/// evidence that seeking is cheap in general. An amortized workload like
/// this can't show what a seek-dense workload costs; that's what
/// `efatfs_core_adversarial_seek_transfer_overhead` (below) is for, where
/// every single read is a long-distance backward seek with nothing to
/// amortize it against.
#[test]
fn efatfs_core_looping_playback_transfer_overhead() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());

    const CLUSTER_SECTORS: u64 = 64; // mk_fixture.sh formats FAT32 with -c 64 (32 KiB)
    let path = "/SAMPLES/huge.bin"; // 64 MiB = 2048 clusters
    let file_len = EFatFs::mount().read_file(path).len() as u64;
    let cluster_bytes = (CLUSTER_SECTORS * 512) as usize;
    let clusters = file_len.div_ceil(cluster_bytes as u64);

    // Loop over the back half: clusters [clusters/2, clusters), repeated. Each
    // wrap is a backward seek across many clusters.
    let loop_start = clusters / 2;
    const PASSES: u64 = 4;
    let reads = PASSES * (clusters - loop_start);
    let data_sectors = reads * CLUSTER_SECTORS; // ideal: data sectors only

    let storage = BufStream::<CountingBlockDevice, 512>::new(CountingBlockDevice);
    let fs: FileSystem<_, DefaultTimeProvider, LossyOemCpConverter> =
        block_on(FileSystem::new(storage, FsOptions::new())).expect("mount counting FS");

    block_on(async {
        let ctx = efatfs_core::open_context(&fs, path).await.expect("open");
        let mut table = HandleTable::<{ efatfs_core::MAX_HANDLES }>::new();
        let h = table.insert(ctx).expect("insert");

        BLOCKS_READ.store(0, Ordering::Relaxed);
        let mut buf = vec![0u8; cluster_bytes];
        for _ in 0..PASSES {
            for c in loop_start..clusters {
                let off = (c * cluster_bytes as u64) as u32;
                assert!(
                    table.read_at_owned(&fs, h, off, &mut buf).await,
                    "loop read at cluster {c} was short"
                );
            }
        }
    });

    let blocks = BLOCKS_READ.load(Ordering::Relaxed);
    let overhead = blocks as f64 / data_sectors as f64;
    println!(
        "lens1-loop-proxy: clusters={clusters} loop_start={loop_start} passes={PASSES} \
         reads={reads} data_sectors={data_sectors} efatfs_block_reads={blocks} \
         overhead={overhead:.2}x (ideal 1.00x = raw-map data-only)"
    );

    // Measured 1.03x (looping playback amortizes its one backward seek per
    // wrap over ~1024 sequential reads) — this is why a cached FAT cluster
    // chain isn't needed; see `efatfs_core_adversarial_seek_transfer_overhead`
    // for the isolated seek-cost measurement. A regression here means
    // looping-sample playback itself got slower, not merely that seeking got
    // more expensive.
    assert!(
        overhead < 1.10,
        "looping-playback transfer overhead {overhead:.2}x regressed \
         (blocks={blocks}, data_sectors={data_sectors}, clusters={clusters})"
    );
}

// --- Adversarial seek-cost isolation ----------------------------------------
//
// The looping-playback proxy above amortizes its one backward seek per wrap
// over ~1024 sequential reads, so it cannot show what seeking itself costs.
// This test isolates that: every single read is a long-distance backward
// seek, with nothing sequential to absorb the FAT-chain re-walk cost into.
//
// Cost model (confirmed by the numbers below): a re-walk to cluster `k`
// touches `ceil(k/128)` FAT sectors, NOT `k` reads — FAT32 packs 128 4-byte
// entries per 512-byte FAT sector, so the re-walk is O(n/128) sector reads,
// not O(n). Against 64 data sectors per 32 KiB cluster, constant
// long-distance seeking costs about `1 + n/16384` for an n-cluster file.
// Measured at n=2048 (64 MB, `/SAMPLES/huge.bin`): **1.15x**, ~9.8 extra
// block reads per read vs a predicted 8.
//
// Consequence: typical Deluge samples (single-digit MB) cost on the order of
// 1.6% even under CONSTANT adversarial seeking, and real looping playback
// (above) costs 1.03x. A cached FAT cluster chain isn't worth adding — the
// re-walk it would eliminate is already cheap at realistic sizes.

/// Two seek-dense access patterns over the same 2048-cluster file, neither of
/// which ever reads two consecutive clusters in file order: reverse
/// (`(0..clusters).rev()`) and ping-pong (alternating ends, walking inward).
/// Each pattern gets a fresh mount so `BufStream`'s single-block cache starts
/// cold and one pattern's tail can't warm the other's head.
#[test]
fn efatfs_core_adversarial_seek_transfer_overhead() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());

    const CLUSTER_SECTORS: u64 = 64; // mk_fixture.sh formats FAT32 with -c 64 (32 KiB)
    let path = "/SAMPLES/huge.bin"; // 64 MiB = 2048 clusters; see mk_fixture.sh for why
    let file_len = EFatFs::mount().read_file(path).len() as u64;
    let cluster_bytes = (CLUSTER_SECTORS * 512) as usize;
    let clusters = file_len.div_ceil(cluster_bytes as u64) as usize;
    let data_sectors = clusters as u64 * CLUSTER_SECTORS; // ideal: one read per data sector

    for (name, reverse) in [("reverse", true), ("pingpong", false)] {
        let cluster_at = |i: usize| -> usize {
            if reverse {
                clusters - 1 - i
            } else if i.is_multiple_of(2) {
                i / 2
            } else {
                clusters - 1 - i / 2
            }
        };

        let storage = BufStream::<CountingBlockDevice, 512>::new(CountingBlockDevice);
        let fs: FileSystem<_, DefaultTimeProvider, LossyOemCpConverter> =
            block_on(FileSystem::new(storage, FsOptions::new())).expect("mount counting FS");

        block_on(async {
            let ctx = efatfs_core::open_context(&fs, path).await.expect("open");
            let mut table = HandleTable::<{ efatfs_core::MAX_HANDLES }>::new();
            let h = table.insert(ctx).expect("insert");

            BLOCKS_READ.store(0, Ordering::Relaxed);
            let mut buf = vec![0u8; cluster_bytes];
            for i in 0..clusters {
                let c = cluster_at(i);
                let off = (c as u64 * cluster_bytes as u64) as u32;
                assert!(
                    table.read_at_owned(&fs, h, off, &mut buf).await,
                    "{name} read at cluster {c} (i={i}) was short"
                );
            }
        });

        let blocks = BLOCKS_READ.load(Ordering::Relaxed);
        let reads = clusters as u64;
        let overhead = blocks as f64 / data_sectors as f64;
        println!(
            "lens1-adversarial-seek-proxy[{name}]: clusters={clusters} reads={reads} \
             data_sectors={data_sectors} efatfs_block_reads={blocks} overhead={overhead:.2}x \
             (ideal 1.00x = raw-map data-only)"
        );

        assert!(
            overhead < 1.30,
            "{name} adversarial-seek transfer overhead {overhead:.2}x too high \
             (blocks={blocks}, data_sectors={data_sectors}, clusters={clusters}) — \
             possible FAT-chain re-walk regression"
        );
    }
}

/// Forward-continue regression proof for `seek()` (file.rs's `impl Seek for
/// File`). The adversarial test above establishes the cost model: a chain
/// re-walk to cluster `k` touches `ceil(k/128)` FAT sectors (FAT32 packs 128
/// 4-byte entries per 512-byte FAT sector), so a 32-cluster file (the old
/// `big_multicluster.bin` fixture) fits in ONE FAT sector regardless of
/// whether `seek()` restarts from `first_cluster` or continues from
/// `current_cluster` — that fixture cannot discriminate the fix from its
/// absence. `/SAMPLES/huge.bin` (64 MiB = 2048 clusters) can: this hops
/// forward 256 clusters at a time from cluster 0 to 1792, landing at k =
/// 256, 512, 768, 1024, 1280, 1536, 1792 (mean k = 1024).
///
/// Without the fix, each hop restarts at `first_cluster`, so cost is
/// `ceil(k/128)` FAT sectors per hop; averaged over the seven hops that is
/// `ceil(1024/128)` = 8 FAT sectors against 64 data sectors/cluster, predicting
/// ~1.125x — **measured 1.154x** (517 blocks / 448 data sectors). With the fix,
/// each hop only walks the 256-cluster delta from `current_cluster`, costing
/// `ceil(256/128)` = 2 FAT sectors, predicting ~1.03x — **measured 1.060x**
/// (475/448). The idealized model undercounts both sides by the same constant:
/// `read_context` reattaches a fresh `File` on every read via
/// `File::new_from_context` (file.rs:76-91, the stale-context guard), which
/// reads+compares the on-disk 32-byte directory entry every time —
/// one more block read per read call, independent of the chain walk, present
/// whether or not this fix applies. The 1.10x bound sits well below the
/// measured broken number (1.154x) and with clear margin above the measured
/// fixed number (1.060x), so a regression back to the `first_cluster` restart
/// still trips it.
#[test]
fn efatfs_forward_seek_does_not_restart_chain_walk() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());

    const CLUSTER_SECTORS: u64 = 64; // mk_fixture.sh formats FAT32 with -c 64 (32 KiB)
    let path = "/SAMPLES/huge.bin"; // 64 MiB = 2048 clusters
    let cluster_bytes = (CLUSTER_SECTORS * 512) as usize;

    let storage = BufStream::<CountingBlockDevice, 512>::new(CountingBlockDevice);
    let fs: FileSystem<_, DefaultTimeProvider, LossyOemCpConverter> =
        block_on(FileSystem::new(storage, FsOptions::new())).expect("mount counting FS");

    block_on(async {
        let ctx = efatfs_core::open_context(&fs, path).await.expect("open");
        let mut table = HandleTable::<{ efatfs_core::MAX_HANDLES }>::new();
        let h = table.insert(ctx).expect("insert");
        let mut buf = vec![0u8; cluster_bytes];

        // Land at an early cluster first, outside the measured window, so the
        // measured hops all start from a real `current_cluster`, not `None`.
        assert!(
            table.read_at_owned(&fs, h, 0, &mut buf).await,
            "initial read"
        );

        BLOCKS_READ.store(0, Ordering::Relaxed);
        let mut reads = 0u64;
        for c in [256u64, 512, 768, 1024, 1280, 1536, 1792] {
            let off = (c * cluster_bytes as u64) as u32;
            assert!(
                table.read_at_owned(&fs, h, off, &mut buf).await,
                "read at cluster {c}"
            );
            reads += 1;
        }
        let blocks = BLOCKS_READ.load(Ordering::Relaxed);
        let data_sectors = reads * CLUSTER_SECTORS;
        let overhead = blocks as f64 / data_sectors as f64;
        println!("forward-seek: reads={reads} blocks={blocks} overhead={overhead:.2}x");

        // Measured 1.154x without the fix (restart from first_cluster) vs
        // 1.060x with it (continue from current_cluster) — see the doc
        // comment above for the full derivation, including the constant
        // per-read directory-entry-validation cost that both numbers carry.
        // 1.10x sits with real margin below the broken number and above the
        // fixed one.
        assert!(
            overhead < 1.10,
            "forward seek overhead {overhead:.2}x suggests seek() is still restarting \
             the chain walk at first_cluster (blocks={blocks}, data_sectors={data_sectors})"
        );
    });
}

/// The AUDIO read access pattern — cluster-aligned reads in playback order,
/// including loop-point backward seeks — must be byte-identical against
/// `/SAMPLES/huge.bin`'s known-good content (`known_huge_bytes`, matching
/// `mk_fixture.sh`'s exact generator formula). Complements the
/// whole-file/interleave tests with the pattern the streaming engine
/// actually issues.
#[test]
fn efatfs_streaming_pattern_matches_known_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let img = fat32();
    let _disk = RamDisk::load(&img);
    let e = EFatFs::mount();

    const CLUSTER_SECTORS: usize = 64; // mk_fixture.sh formats FAT32 -c 64 (32 KiB clusters)
    const CLUSTER_BYTES: usize = CLUSTER_SECTORS * 512;
    let path = "/SAMPLES/huge.bin"; // 64 MiB = 2048 clusters

    let oracle = known_huge_bytes();
    let clusters = oracle.len().div_ceil(CLUSTER_BYTES);
    assert!(clusters > 4, "fixture too small to exercise the pattern");

    // Playback-order sequence with loop-point backward seeks: forward through the
    // file, then loop the back half twice (the case efatfs's seek() re-walk fix and
    // the streaming path care about).
    let mut order: Vec<usize> = (0..clusters).collect();
    let loop_start = clusters / 2;
    for _ in 0..2 {
        order.extend(loop_start..clusters);
    }

    let ctx = e.open_context(path);
    let mut ctx = ctx;
    for c in order {
        let off = (c * CLUSTER_BYTES) as u32;
        let want = CLUSTER_BYTES.min(oracle.len() - c * CLUSTER_BYTES);
        let mut buf = vec![0u8; want];
        let (newctx, filled) = e.read_at_context(&ctx, off, &mut buf);
        ctx = newctx;
        assert!(filled, "efatfs short read at cluster {c} (off {off})");
        assert_eq!(
            &buf[..],
            &oracle[c * CLUSTER_BYTES..c * CLUSTER_BYTES + want],
            "efatfs streaming-pattern read diverged from the known-good content at cluster {c}"
        );
    }
}

/// Non-vacuity check: reading the WRONG cluster must NOT match the oracle — proves the
/// streaming check above can actually detect a divergence (per the project's
/// real-execution mandate). If this ever passes-as-equal, the check is blind.
#[test]
fn efatfs_streaming_pattern_nonvacuous() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let img = fat32();
    let _disk = RamDisk::load(&img);
    let e = EFatFs::mount();

    const CLUSTER_BYTES: usize = 64 * 512;
    let path = "/SAMPLES/huge.bin";
    let oracle = known_huge_bytes();

    // Read cluster 3 but compare against the oracle's cluster 0 slice — a real
    // multi-cluster file has distinct cluster contents unless the test is blind.
    let ctx = e.open_context(path);
    let mut buf = vec![0u8; CLUSTER_BYTES];
    let (_ctx, filled) = e.read_at_context(&ctx, (3 * CLUSTER_BYTES) as u32, &mut buf);
    assert!(filled, "short read");
    assert_ne!(
        &buf[..],
        &oracle[0..CLUSTER_BYTES],
        "cluster 3 matched the oracle's cluster 0 — the fixture's clusters are not \
         distinguishable, so the streaming check cannot detect a wrong-offset read"
    );
}

/// The sector-rounded LAST-CLUSTER read the streaming loader issues
/// (`begin_fill` in `async_fill.cpp`) legitimately requests bytes past the file's
/// logical EOF — `numSectors = ceil((audioDataEnd - clusterStart) / 512)` rounds the
/// read up to a whole number of 512-byte sectors, and `audioDataEnd` is frequently
/// NOT sector-aligned (e.g. a WAV whose `data` chunk ends the file). The now-deleted
/// C-FatFS raw-sector reader tolerated this by design (the last cluster's on-disk
/// allocation is always >= file_size, padded up to the cluster boundary). This proves
/// `efatfs_core`'s streaming read now tolerates it too: a read whose requested range
/// extends a few hundred bytes past logical EOF (but stays within the same cluster)
/// must succeed (`filled == true`), return the real bytes up to EOF, and zero-pad the
/// rest — instead of failing outright as it did before the fix (see the git history of
/// `efatfs_core::fill` for the pre-fix behavior this test caught).
#[test]
fn efatfs_core_fill_zero_pads_past_logical_eof_last_cluster() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let img = fat32();
    let _disk = RamDisk::load(&img);
    let e = EFatFs::mount();
    let fs = e.raw();

    const CLUSTER_BYTES: usize = 64 * 512; // mk_fixture.sh formats FAT32 -c 64 (32 KiB clusters)
    let path = "/SAMPLES/huge.bin"; // 64 MiB, exactly cluster-aligned (2048 * 32 KiB)
    let oracle = known_huge_bytes();
    let file_len = oracle.len();

    // Emulate a sector-rounded last-cluster read: start near EOF, request a length
    // that overruns the logical file size by a few hundred bytes but stays well
    // under one cluster.
    let overrun = 300usize;
    let offset = file_len - 200;
    let len = 200 + overrun;
    assert!(
        len < CLUSTER_BYTES,
        "test overrun must stay within one cluster"
    );
    let oracle_remaining = file_len - offset;

    let mut buf = vec![0xAAu8; len]; // non-zero fill so the zero-pad assertion is meaningful
    let filled = block_on(async {
        let ctx = efatfs_core::open_context(fs, path)
            .await
            .expect("open_context huge.bin");
        let (_ctx, filled) = efatfs_core::read_context(fs, ctx, offset as u32, &mut buf)
            .await
            .expect("read_context returned None (FS error)");
        filled
    });

    assert!(
        filled,
        "efatfs read past logical EOF (offset={offset}, len={len}, file_len={file_len}) \
         was NOT tolerated — the last-cluster sector-rounded read must succeed"
    );
    assert_eq!(
        &buf[..oracle_remaining],
        &oracle[offset..file_len],
        "bytes up to logical EOF must match the real file contents"
    );
    assert!(
        buf[oracle_remaining..].iter().all(|&b| b == 0),
        "bytes past logical EOF must be zero-padded, not left as read-buffer garbage"
    );
}

/// Byte-offset read-exactness at a non-cluster-aligned offset — the operation
/// `deluge_efatfs_read_at` performs (arbitrary byte_offset, arbitrary length), proven byte-exact
/// against the known-good content. (The FFI's block_on bridge is thin glue over exactly this call.)
#[test]
fn efatfs_read_at_arbitrary_offset_matches_known_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let img = fat32();
    let _disk = RamDisk::load(&img);
    let e = EFatFs::mount();

    let path = "/SAMPLES/huge.bin";
    let oracle = known_huge_bytes();

    // Offsets deliberately NOT on cluster boundaries, spanning cluster edges.
    let ctx = e.open_context(path);
    let mut ctx = ctx;
    for &(off, len) in &[(0usize, 100usize), (33_000, 40_000), (1_000_003, 12_345)] {
        let end = (off + len).min(oracle.len());
        let mut buf = vec![0u8; end - off];
        let (newctx, filled) = e.read_at_context(&ctx, off as u32, &mut buf);
        ctx = newctx;
        assert!(filled, "efatfs short read at off {off}");
        assert_eq!(
            &buf[..],
            &oracle[off..end],
            "efatfs read diverged at off {off}"
        );
    }
}

/// Write-then-read round-trip through the core write/read-exact
/// primitives, including the EOF-honest read's contract — a request for MORE
/// bytes than the file holds must return the true short count, NOT a
/// zero-padded full buffer (that's `read_context`/`fill`'s streaming-read
/// behavior, deliberately NOT this one's).
#[test]
fn efatfs_write_then_read_roundtrip_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
    // Create + write a known pattern, then read it back byte-exact.
    let path = "/R2WRITE.BIN";
    let payload: Vec<u8> = (0..5000u32).map(|i| (i * 7) as u8).collect();
    let ctx = e.create_context(path, false); // WRITE_CREATE
    let (ctx, w) = e.write_at_context(&ctx, 0, &payload);
    assert_eq!(w, payload.len());
    let (ctx, sz) = e.size_context(&ctx);
    assert_eq!(sz, payload.len() as u32);
    // EOF-honest read: request MORE than the file holds, get the true short count (not zero-padded).
    let mut buf = vec![0u8; payload.len() + 512];
    let (_ctx, n) = e.read_exact_context(&ctx, 0, &mut buf);
    assert_eq!(
        n,
        payload.len(),
        "read_context_exact must return the true byte count, not zero-padded"
    );
    assert_eq!(&buf[..n], &payload[..]);
}

/// `truncate_context` shrinks the on-disk size, confirmed via
/// `size_context`.
#[test]
fn efatfs_truncate_shrinks_size_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
    let path = "/R2TRUNC.BIN";
    let ctx = e.create_context(path, false);
    let (ctx, _) = e.write_at_context(&ctx, 0, &vec![0xABu8; 4096]);
    let (ctx, _) = e.truncate_context(&ctx, 1000);
    let (_ctx, sz) = e.size_context(&ctx);
    assert_eq!(sz, 1000);
}

/// mkdir + create-in-dir + rename + unlink, each verified via a FRESH
/// [`EFatFs::mount`] (a brand-new `FileSystem` over the same shared `DISK`,
/// re-walking the on-disk directory structures from scratch) so the check
/// proves the op actually landed on disk, not just that the writer's own
/// in-memory `EFatFs` view agrees with itself.
#[test]
fn efatfs_create_unlink_rename_mkdir_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
    // mkdir + create-in-dir + rename + unlink, verifying via a fresh mount that each landed on disk.
    e.mkdir("/R2DIR");
    let ctx = e.create_context("/R2DIR/A.BIN", false);
    let (_ctx, _) = e.write_at_context(&ctx, 0, b"hello");
    assert!(
        EFatFs::mount().exists("/R2DIR/A.BIN"),
        "efatfs create not visible to a fresh mount"
    );
    e.rename("/R2DIR/A.BIN", "/R2DIR/B.BIN");
    assert!(EFatFs::mount().exists("/R2DIR/B.BIN") && !EFatFs::mount().exists("/R2DIR/A.BIN"));
    e.unlink("/R2DIR/B.BIN");
    assert!(!EFatFs::mount().exists("/R2DIR/B.BIN"));
}

/// WRITE_CREATE_NEW (`exclusive == true`) must fail — not
/// silently truncate — when the target path already exists.
#[test]
fn efatfs_create_new_fails_if_exists_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
    let _ = e.create_context("/R2EXCL.BIN", false); // create it
    assert!(
        e.try_create_exclusive("/R2EXCL.BIN").is_none(),
        "WRITE_CREATE_NEW must fail on existing"
    );
}

/// `set_time` writes a packed FAT date/time (`(dos_date << 16) | dos_time`)
/// that a FRESH [`EFatFs::mount`] reads back identically — the same
/// fresh-mount cross-check the create/rename/unlink test above uses.
#[test]
fn efatfs_set_time_roundtrips_via_fresh_mount_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
    let path = "/R2TIME.BIN";
    let _ = e.create_context(path, false);

    // 2019-03-04 05:06:08, packed the same way `set_time`/`mtime` document:
    // high 16 bits DOS date ((year-1980)<<9 | month<<5 | day), low 16 bits
    // DOS time (hour<<11 | min<<5 | sec/2).
    let dos_date: u32 = ((2019u32 - 1980) << 9) | (3 << 5) | 4;
    let dos_time: u32 = (5 << 11) | (6 << 5) | (8 / 2);
    let timestamp = (dos_date << 16) | dos_time;

    e.set_time(path, timestamp);

    assert_eq!(
        EFatFs::mount().mtime(path),
        timestamp,
        "efatfs set_time not visible (or mismatched) via a fresh mount"
    );
}

// --- Directory enumeration ---------------------------------------

/// efatfs's directory listing of the fixture's `/SAMPLES` matches the
/// known-good expected SET of (name, is_dir, size) tuples (order not
/// compared).
#[test]
fn efatfs_readdir_matches_known_set_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
    let dir = "/SAMPLES"; // exists in the fixture tree
    let mut got: Vec<(String, bool, u32)> = e.readdir_all(dir);
    got.sort();
    assert_eq!(
        got,
        known_samples_dir_set(),
        "efatfs directory listing diverged from the known-good expected set"
    );
}

/// The `/SAMPLES` fixture directory's known-good expected entries, as a
/// sorted set of (name, is_dir, size). `hello.txt` is 11 bytes
/// (`b"DELUGE-SP0\n"`); `big_multicluster.bin`/`huge.bin` sizes are fixed by
/// `fixtures/mk_fixture.sh`'s generator; `Kicks` is a subdirectory (size 0
/// by this harness's `Entry` convention).
fn known_samples_dir_set() -> Vec<(String, bool, u32)> {
    let mut v = vec![
        ("Kicks".to_string(), true, 0u32),
        ("big_multicluster.bin".to_string(), false, 1_048_576),
        ("hello.txt".to_string(), false, 11),
        ("huge.bin".to_string(), false, 67_108_864),
    ];
    v.sort();
    v
}

/// The load-bearing proof for the persistent-write-handle
/// mechanism the sample recorder needs -- write N clusters through the
/// no-flush primitives (accumulating ONLY the in-memory size), confirm a
/// FRESH open-by-path mount still sees the stale (pre-write) on-disk size,
/// confirm a read back THROUGH the write context sees the true in-memory
/// extent (byte-exact), then flush once and confirm a fresh mount now sees
/// the full size.
#[test]
fn efatfs_noflush_write_then_read_via_context_before_flush_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
    let path = "/R3REC.BIN";
    const CB: usize = 64 * 512; // one 32KiB cluster (fixture geometry)
                                // Create empty, get a write context.
    let ctx = e.create_context(path, false);
    // Write 3 clusters with DISTINCT content, NO flush between.
    let mut ctx = ctx;
    let mut clusters: Vec<Vec<u8>> = vec![];
    for c in 0..3u32 {
        let payload: Vec<u8> = (0..CB).map(|i| ((c as usize + i) & 0xff) as u8).collect();
        let (nc, w) = e.write_at_via_context_noflush(&ctx, c * CB as u32, &payload);
        assert_eq!(w, CB);
        ctx = nc;
        clusters.push(payload);
    }
    // On-disk dir size is STILL STALE (no flush) — a fresh open-by-path mount sees ~0 bytes.
    assert_eq!(
        EFatFs::mount().read_file(path).len(),
        0,
        "precondition: dir size not yet flushed"
    );
    // But reading cluster 0 back THROUGH THE WRITE CONTEXT succeeds (in-memory size = written extent).
    let mut buf = vec![0u8; CB];
    let (nc, n) = e.read_at_via_context(&ctx, 0, &mut buf);
    ctx = nc;
    assert_eq!(n, CB);
    assert_eq!(&buf[..], &clusters[0][..], "mid-write read-back diverged");
    // Finalize flush persists the size; now a fresh mount sees all 3 clusters.
    let _ctx = e.flush_context(&ctx);
    assert_eq!(
        EFatFs::mount().read_file(path).len(),
        3 * CB,
        "flush must persist the full size"
    );
}

/// The REAL `efatfs_core::readdir_open`/`readdir_next` (not the
/// `EFatFs` wrapper above) walked directly, matching the known-good
/// expected set. (The UI identifies files by path, not by a held FS
/// locator.)
#[test]
fn efatfs_core_readdir_matches_known_set_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let efatfs = EFatFs::mount();
    let fs = efatfs.raw();

    let mut got: Vec<(String, bool, u32)> = Vec::new();

    block_on(async {
        let mut cursor = efatfs_core::readdir_open(fs, "/SAMPLES")
            .await
            .expect("readdir_open /SAMPLES");
        loop {
            match efatfs_core::readdir_next(&mut cursor) {
                Some(Some(info)) => {
                    got.push((info.name.as_str().to_string(), info.is_dir, info.size));
                }
                Some(None) => break, // EOF
                None => panic!("readdir_next returned an FS error"),
            }
        }
    });

    got.sort();
    assert_eq!(
        got,
        known_samples_dir_set(),
        "efatfs_core readdir diverged from the known-good expected set"
    );
}

// --- Mid-write read-back via the write context -----------------------------
//
// `deluge::io::Stream` (stream.cpp) routes the sample recorder's writes through the
// `deluge_efatfs_stream_*` C-ABI (`efatfs_fs.rs`/`efatfs_host_shim.rs`), which composes the SAME
// `write_context_noflush`/`flush_context`/`read_at_via_context` primitives exercised here --
// fs_differential only `#[path]`-includes `efatfs_core.rs` (see this file's module doc) and does
// not link the `deluge-bsp-rust` binary crate the C-ABI `extern "C"` symbols live in, so the
// C-ABI wiring itself is instead covered by `cargo build -p deluge-bsp-rust --features
// host_app,efatfs_streaming` + `cargo test -p deluge-bsp-rust --target x86_64-unknown-linux-gnu`
// (the whole host suite, which builds and links those symbols). This test proves the primitive
// underneath them: a recording-shaped sequential multi-cluster write through the no-flush write
// path, then EVERY already-written cluster -- not just the most recently written one -- reading
// back byte-exact through the SAME still-open, unflushed write context, and a final flush+close
// making the whole file visible byte-exact to a fresh mount.

/// A recording-shaped sequential write via the efatfs
/// persistent-context write primitives, no flush between clusters, then -- mirroring how the
/// sample recorder's `alterFile()`/finalize header patch-back read their own still-open write
/// context back via `read_at_via` -- every EARLIER cluster read back byte-exact through the SAME
/// still-open write context via
/// `read_at_via_context`, all while the on-disk directory entry is still stale (proving the
/// write context's IN-MEMORY size, not the stale on-disk size, is what bounds the read). Finally
/// closed+flushed and read back byte-exact via a FRESH mount, proving the persistent-context
/// Stream write path round-trips end to end.
#[test]
fn efatfs_stream_mid_write_read_back_via_context_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
    let path = "/R3READ.BIN";
    const CB: usize = 64 * 512; // one 32KiB cluster (fixture geometry)
    const NUM_CLUSTERS: u32 = 4;

    let mut ctx = e.create_context(path, false); // WRITE_CREATE
    let mut clusters: Vec<Vec<u8>> = Vec::new();
    for c in 0..NUM_CLUSTERS {
        // Distinct-per-cluster content so a mixed-up cluster read fails loudly.
        let payload: Vec<u8> = (0..CB)
            .map(|i| ((c as usize * 31 + i) & 0xff) as u8)
            .collect();
        let (newctx, w) = e.write_at_via_context_noflush(&ctx, c * CB as u32, &payload);
        assert_eq!(
            w, CB,
            "write_at_via_context_noflush must write a whole cluster"
        );
        ctx = newctx;
        clusters.push(payload);
    }

    // On-disk dir size is still stale pre-flush (same precondition the write/read-via-context test checks).
    assert_eq!(
        EFatFs::mount().read_file(path).len(),
        0,
        "precondition: dir size not yet flushed"
    );

    // Mid-write read-back: read EVERY already-written cluster -- including the
    // earliest, long since superseded as "most recently written" -- back through the same
    // still-open, unflushed write context. The write context's in-memory size covers the whole
    // written extent even though the on-disk directory entry is stale (proven above), so this
    // must succeed and be byte-exact for every index, not just the last one written.
    for c in 0..NUM_CLUSTERS {
        let mut buf = vec![0u8; CB];
        let (newctx, n) = e.read_at_via_context(&ctx, c * CB as u32, &mut buf);
        ctx = newctx;
        assert_eq!(
            n, CB,
            "read_at_via_context must read a whole cluster for cluster {c}"
        );
        assert_eq!(
            buf, clusters[c as usize],
            "cluster {c} read back via the write context doesn't match what was written"
        );
    }

    // Finalize: flush persists the accumulated size/mtime edit (`Stream::close`'s efatfs branch).
    let _ctx = e.flush_context(&ctx);

    // Fresh mount: byte-exact round trip of every cluster written, in order.
    let want: Vec<u8> = clusters.into_iter().flatten().collect();
    let got = EFatFs::mount().read_file(path);
    assert_eq!(
        got.len(),
        want.len(),
        "flushed file size doesn't match the total bytes written"
    );
    assert_eq!(
        got, want,
        "flushed file contents diverged from what was written"
    );
}

// --- Finalize header-patch positional write -----------------------------------

/// Write a recording-shaped multi-cluster file through the no-flush
/// write primitives, then -- on the SAME still-open write context, mirroring
/// `SampleRecorder::finalizeRecordedFile`'s chosen ordering (patch the WAV header via
/// `Stream::write_at(0, first-sector-span)` BEFORE `Stream::close()`, since `write_at` needs an
/// open handle) -- overwrite just the first 512-byte sector at offset 0 with a distinct "patched
/// header" pattern via a second no-flush positional write. Flush+close, then read the WHOLE file
/// back via a FRESH mount and assert the first sector reflects the patch AND every byte after it
/// is untouched -- proving a positional write back to offset 0 composes correctly with a prior
/// sequential multi-cluster write under the SAME persistent write context.
#[test]
fn efatfs_write_at_header_patch_after_body_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
    let path = "/R3HDR.BIN";
    const CB: usize = 64 * 512; // one 32KiB cluster (fixture geometry)
    const NUM_CLUSTERS: u32 = 3;
    const SECTOR: usize = 512;

    // Write the recording body: NUM_CLUSTERS distinct-content clusters, no flush between (the
    // sample recorder's RT write loop, `SampleRecorder::writeCluster`).
    let mut ctx = e.create_context(path, false); // WRITE_CREATE
    let mut clusters: Vec<Vec<u8>> = Vec::new();
    for c in 0..NUM_CLUSTERS {
        let payload: Vec<u8> = (0..CB)
            .map(|i| ((c as usize * 17 + i) & 0xff) as u8)
            .collect();
        let (nc, w) = e.write_at_via_context_noflush(&ctx, c * CB as u32, &payload);
        assert_eq!(w, CB, "body write must write a whole cluster");
        ctx = nc;
        clusters.push(payload);
    }

    // The finalize header patch: a second, independent positional write at offset 0, still on
    // the same open context -- `SampleRecorder::finalizeRecordedFile`'s
    // `Stream::write_at(0, first-sector-span)` before `Stream::close()`.
    let patched_header: Vec<u8> = (0..SECTOR).map(|i| 0xEEu8 ^ (i as u8)).collect();
    let (nc, w) = e.write_at_via_context_noflush(&ctx, 0, &patched_header);
    assert_eq!(
        w, SECTOR,
        "header-patch write must write the full first sector"
    );
    ctx = nc;

    // Finalize: flush persists the accumulated size/mtime edit and every write (patch included) --
    // `Stream::close`'s efatfs branch.
    let _ctx = e.flush_context(&ctx);

    // A fresh mount proves the patch landed on disk (not just in the in-memory
    // context), and that the rest of the recorded body is untouched by it.
    let got = EFatFs::mount().read_file(path);
    let mut want: Vec<u8> = clusters.into_iter().flatten().collect();
    want[..SECTOR].copy_from_slice(&patched_header);

    assert_eq!(
        got.len(),
        want.len(),
        "flushed file size must be unaffected by the header patch"
    );
    assert_eq!(
        &got[..SECTOR],
        &patched_header[..],
        "first sector must reflect the header patch"
    );
    assert_eq!(
        &got[SECTOR..],
        &want[SECTOR..],
        "bytes after the patched header sector must be byte-identical to the originally written body"
    );
}

// --- alterFile in-place middle-cluster rewrite ---------------------------------

/// Write a multi-cluster file, then rewrite a MIDDLE cluster IN
/// PLACE via `write_at(middleIndex << mag, newSpan)` -- mirroring
/// `SampleRecorder::alterFile`'s two positional-write sites, which rewrite already-recorded
/// clusters at their own byte offset (`clusterIndex << Cluster::size_magnitude`) through a write
/// context reopened (`DELUGE_STREAM_WRITE_APPEND` -- `open_context`, no truncation) at the top of
/// the alteration and held open across every rewrite -- then flush, and read back via a FRESH
/// mount. Asserts the rewritten cluster holds the new content AND every surrounding cluster
/// (both before and after it) is byte-identical to what was originally written -- an in-place
/// rewrite must not disturb its neighbors, and the file's total size must not change (no
/// truncation happened, unlike `alterFile`'s own end-of-alteration truncate, which is a separate,
/// already-covered primitive -- `truncate_context`/`Stream::truncate`).
#[test]
fn efatfs_write_at_middle_cluster_rewrite_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
    let path = "/R3ALTER.BIN";
    const CB: usize = 64 * 512; // one 32KiB cluster (fixture geometry)
    const NUM_CLUSTERS: u32 = 4;
    const MIDDLE: u32 = 1; // neither the first nor the last cluster

    // Write the original recording: NUM_CLUSTERS distinct-content clusters, no flush between
    // (SampleRecorder::writeCluster's RT write loop), then flush+close -- the file as it exists
    // right before alterFile is called.
    let mut ctx = e.create_context(path, false); // WRITE_CREATE
    let mut clusters: Vec<Vec<u8>> = Vec::new();
    for c in 0..NUM_CLUSTERS {
        let payload: Vec<u8> = (0..CB)
            .map(|i| ((c as usize * 37 + i) & 0xff) as u8)
            .collect();
        let (nc, w) = e.write_at_via_context_noflush(&ctx, c * CB as u32, &payload);
        assert_eq!(w, CB, "original-recording write must write a whole cluster");
        ctx = nc;
        clusters.push(payload);
    }
    let _ctx = e.flush_context(&ctx);

    // alterFile's own reopen: DELUGE_STREAM_WRITE_APPEND opens the existing file without
    // truncating it (`efatfs_fs.rs`'s `stream_open` mode 3 -> `open_context`, same as READ) --
    // this is the write context alterFile holds open for the whole in-place rewrite pass.
    let mut alter_ctx = e.open_context(path);

    // Rewrite ONLY the middle cluster, in place, at its own byte offset -- alterFile's mid-loop
    // `Stream::write_at(currentWriteClusterIndex << Cluster::size_magnitude, span)`.
    let new_middle: Vec<u8> = (0..CB).map(|i| 0xC3u8 ^ (i as u8)).collect();
    let (nc, w) = e.write_at_via_context_noflush(&alter_ctx, MIDDLE * CB as u32, &new_middle);
    assert_eq!(w, CB, "in-place rewrite must write a whole cluster");
    alter_ctx = nc;

    // Finalize: flush+close, same as alterFile's end-of-alteration `Stream::close` (no truncate
    // in this test -- the rewrite didn't change the file's size).
    let _alter_ctx = e.flush_context(&alter_ctx);

    // Fresh mount: the rewritten cluster must hold the NEW content, and every other
    // cluster must be untouched.
    let got = EFatFs::mount().read_file(path);
    assert_eq!(
        got.len(),
        NUM_CLUSTERS as usize * CB,
        "in-place rewrite must not change the file's total size"
    );
    for c in 0..NUM_CLUSTERS {
        let start = c as usize * CB;
        let end = start + CB;
        let want: &[u8] = if c == MIDDLE {
            &new_middle
        } else {
            &clusters[c as usize]
        };
        assert_eq!(
            &got[start..end],
            want,
            "cluster {c}'s content diverged from what it should hold after the in-place rewrite"
        );
    }
}
