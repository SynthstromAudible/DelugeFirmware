//! Host tests of the BSP's storage-generic efatfs core, `storage::efatfs::portable`.
//!
//! The core runs over `fs_differential`'s RAM-disk `FileSystem`, covering the handle table's
//! generation guard, `FileContext` detach/reattach, the fill loop, the write primitives the
//! recorder's stream uses, and the read cost of FAT-chain walks. Tests are single-threaded, so
//! they do not cover the device's mutex serialization.

use aligned::{Aligned, A4};
use block_device_adapters::BufStream;
use block_device_driver::BlockDevice;
use deluge_bsp_rust::storage::efatfs::portable::{self as efatfs_core, HandleTable};
use embassy_futures::block_on;
use embedded_fatfs::{DefaultTimeProvider, FileSystem, FsOptions, LossyOemCpConverter};
use fs_differential::{efatfs::EFatFs, ram_disk::RamDisk};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

// Serializes tests over the process-wide RAM disk.
static TEST_LOCK: Mutex<()> = Mutex::new(());

fn fat32() -> String {
    std::env::var("SP0_FAT32").expect("run mk_fixture.sh; set SP0_FAT32=/tmp/fat32.img")
}

/// `mk_fixture.sh`'s `/SAMPLES/huge.bin` (FAT32 only): 64 MiB, byte `i` is `(i >> 9) & 0xff`.
fn known_huge_bytes() -> Vec<u8> {
    (0..67_108_864u64)
        .map(|i| ((i >> 9) & 0xff) as u8)
        .collect()
}

/// `open_context` and `HandleTable::read_at_owned` read correct bytes, alone and with two handles
/// interleaved.
#[test]
fn efatfs_core_open_read_and_interleave_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let efatfs = EFatFs::mount();
    let fs = efatfs.raw();
    let mut table = HandleTable::<{ efatfs_core::MAX_HANDLES }>::new();

    block_on(async {
        // Single handle.
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

        // Two handles read round-robin; each reassembled file must match a whole-file read.
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

/// A `checkout` taken before its slot is removed and reused cannot `commit` its stale context onto
/// the new file: the device `read_at` write-back race the generation guard exists for.
#[test]
fn efatfs_core_generation_guard_rejects_stale_commit() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let efatfs = EFatFs::mount();
    let fs = efatfs.raw();
    let mut table = HandleTable::<{ efatfs_core::MAX_HANDLES }>::new();

    block_on(async {
        let kick_path = "/SAMPLES/Kicks/Deep House Kick (loud).wav";

        let hello_ctx = efatfs_core::open_context(fs, "/SAMPLES/hello.txt")
            .await
            .expect("open hello");
        let h = table.insert(hello_ctx).expect("insert hello");
        let (stale_gen, stale_ctx) = table.checkout(h).expect("checkout hello");

        // `remove` bumps the generation; `insert` reuses the lowest free index.
        table.remove(h);
        let kick_ctx = efatfs_core::open_context(fs, kick_path)
            .await
            .expect("open kick");
        let h2 = table.insert(kick_ctx).expect("insert kick");
        assert_eq!(h2, h, "insert should reuse the freed lowest index");

        table.commit(h, stale_gen, stale_ctx);

        // The recycled handle must still read the kick file.
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

// Transfer-overhead tests: count the 512-byte block reads embedded-fatfs issues for an access
// pattern and compare with the ideal of one read per data sector. The per-read FAT-chain walk is
// the cost that matters; an O(n) re-walk on every read would show up here.

/// 512-byte blocks read since the last reset; serialized by `TEST_LOCK`.
static BLOCKS_READ: AtomicU64 = AtomicU64::new(0);

/// A RAM-disk `BlockDevice<512>` that counts blocks read into [`BLOCKS_READ`].
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

    // mk_fixture.sh formats FAT32 with `-c 64`: 32 KiB clusters.
    const CLUSTER_SECTORS: u64 = 64;
    let kick_path = "/SAMPLES/big_multicluster.bin"; // 1 MiB = 32 clusters

    // File size through an uncounted mount.
    let file_len = EFatFs::mount().read_file(kick_path).len() as u64;
    let data_sectors = file_len.div_ceil(512); // ideal: one read per data sector
    let clusters = file_len.div_ceil(CLUSTER_SECTORS * 512);

    // Read the file sequentially, a cluster at a time, over the counting device.
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
        "margin-proxy: file={file_len}B clusters={clusters} data_sectors={data_sectors} \
         efatfs_block_reads={blocks} overhead={overhead:.2}x (ideal 1.00x = raw-map data-only)"
    );

    // A few FAT reads per cluster are expected; an O(n) re-walk per cluster would scale with
    // `clusters`. The generous bound catches that, not tuning noise.
    assert!(
        overhead < 3.0,
        "efatfs sequential-read transfer overhead {overhead:.2}x too high \
         (blocks={blocks}, data_sectors={data_sectors}, clusters={clusters}) — \
         possible O(n) FAT-chain re-walk regression"
    );
}

/// A sample looping over its back half: one backward seek per wrap, then ~1024 sequential cluster
/// reads.
///
/// The backward seek re-walks the chain from the first cluster, amortized over the sequential
/// reads. This measures looping playback, not seek cost; see
/// [`efatfs_core_adversarial_seek_transfer_overhead`].
#[test]
fn efatfs_core_looping_playback_transfer_overhead() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());

    const CLUSTER_SECTORS: u64 = 64; // mk_fixture.sh formats FAT32 with -c 64 (32 KiB)
    let path = "/SAMPLES/huge.bin"; // 64 MiB = 2048 clusters
    let file_len = EFatFs::mount().read_file(path).len() as u64;
    let cluster_bytes = (CLUSTER_SECTORS * 512) as usize;
    let clusters = file_len.div_ceil(cluster_bytes as u64);

    // Loop over clusters [clusters / 2, clusters).
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
        "margin-loop-proxy: clusters={clusters} loop_start={loop_start} passes={PASSES} \
         reads={reads} data_sectors={data_sectors} efatfs_block_reads={blocks} \
         overhead={overhead:.2}x (ideal 1.00x = raw-map data-only)"
    );

    // Looping playback stays near 1.0x, which is why there is no cached FAT cluster chain. A
    // failure here means looping playback itself got slower.
    assert!(
        overhead < 1.10,
        "looping-playback transfer overhead {overhead:.2}x regressed \
         (blocks={blocks}, data_sectors={data_sectors}, clusters={clusters})"
    );
}

/// Seek cost in isolation: every read is a long backward seek, in two patterns that never read
/// consecutive clusters (reverse, and ping-pong walking inward from both ends).
///
/// A re-walk to cluster `k` reads `ceil(k / 128)` FAT sectors (128 FAT32 entries per sector), so
/// constant seeking over an n-cluster file of 32 KiB clusters costs about `1 + n / 16384`. At
/// realistic sample sizes that is a few percent, so a cached FAT cluster chain is not worth adding.
/// Each pattern gets a fresh mount so `BufStream`'s block cache starts cold.
#[test]
fn efatfs_core_adversarial_seek_transfer_overhead() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());

    const CLUSTER_SECTORS: u64 = 64; // mk_fixture.sh formats FAT32 with -c 64 (32 KiB)
    let path = "/SAMPLES/huge.bin"; // 64 MiB = 2048 clusters
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
            "margin-adversarial-seek-proxy[{name}]: clusters={clusters} reads={reads} \
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

/// A forward `seek` continues the chain walk from the current cluster instead of restarting at the
/// first.
///
/// Hops forward 256 clusters at a time through the 2048-cluster `huge.bin`; a small file fits in
/// one FAT sector and could not tell the two apart. Restarting costs about 1.15x, continuing about
/// 1.06x (each reattach also re-reads the directory entry); the 1.10x bound separates them.
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

        // Start from a real current cluster, outside the measured window.
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

        assert!(
            overhead < 1.10,
            "forward seek overhead {overhead:.2}x suggests seek() is still restarting \
             the chain walk at first_cluster (blocks={blocks}, data_sectors={data_sectors})"
        );
    });
}

/// The streaming engine's access pattern (cluster reads in playback order with loop-point backward
/// seeks) reads `huge.bin` byte-identical to [`known_huge_bytes`].
#[test]
fn efatfs_playback_pattern_matches_known_fat32() {
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

    // Forward through the file, then loop the back half twice.
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

/// Non-vacuity check for [`efatfs_playback_pattern_matches_known_fat32`]: a wrong-cluster read
/// must not match.
#[test]
fn efatfs_playback_pattern_nonvacuous() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let img = fat32();
    let _disk = RamDisk::load(&img);
    let e = EFatFs::mount();

    const CLUSTER_BYTES: usize = 64 * 512;
    let path = "/SAMPLES/huge.bin";
    let oracle = known_huge_bytes();

    // Read cluster 3; compare against cluster 0.
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

/// A streaming read that runs past logical EOF succeeds, returns the real bytes, and zero-pads
/// the rest.
///
/// The streaming loader rounds its last-cluster read up to whole sectors, and audio data often
/// ends mid-sector, so such reads are routine.
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

    // Overrun EOF by a few hundred bytes, within one cluster.
    let overrun = 300usize;
    let offset = file_len - 200;
    let len = 200 + overrun;
    assert!(
        len < CLUSTER_BYTES,
        "test overrun must stay within one cluster"
    );
    let oracle_remaining = file_len - offset;

    let mut buf = vec![0xAAu8; len]; // non-zero, so the zero-pad check means something
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

/// Reads at arbitrary, non-cluster-aligned offsets and lengths (what `deluge_efatfs_read_at`
/// does) are byte-exact.
#[test]
fn efatfs_read_at_arbitrary_offset_matches_known_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let img = fat32();
    let _disk = RamDisk::load(&img);
    let e = EFatFs::mount();

    let path = "/SAMPLES/huge.bin";
    let oracle = known_huge_bytes();

    // Offsets off cluster boundaries, spanning cluster edges.
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

/// A write reads back exactly, and an exact read past EOF returns the true short count rather
/// than zero-padding like the streaming read.
#[test]
fn efatfs_write_then_read_roundtrip_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
    let path = "/R2WRITE.BIN";
    let payload: Vec<u8> = (0..5000u32).map(|i| (i * 7) as u8).collect();
    let ctx = e.create_context(path, false); // WRITE_CREATE
    let (ctx, w) = e.write_at_context(&ctx, 0, &payload);
    assert_eq!(w, payload.len());
    let (ctx, sz) = e.size_context(&ctx);
    assert_eq!(sz, payload.len() as u32);
    // Request more than the file holds.
    let mut buf = vec![0u8; payload.len() + 512];
    let (_ctx, n) = e.read_exact_context(&ctx, 0, &mut buf);
    assert_eq!(
        n,
        payload.len(),
        "read_context_exact must return the true byte count, not zero-padded"
    );
    assert_eq!(&buf[..n], &payload[..]);
}

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

/// mkdir, create, rename and unlink each land on disk, checked through a fresh mount.
#[test]
fn efatfs_create_unlink_rename_mkdir_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
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

/// An exclusive create fails, rather than truncating, when the path exists.
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

/// `set_time`'s packed FAT date/time reads back identically through a fresh mount.
#[test]
fn efatfs_set_time_roundtrips_via_fresh_mount_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
    let path = "/R2TIME.BIN";
    let _ = e.create_context(path, false);

    // 2019-03-04 05:06:08.
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

#[test]
fn efatfs_readdir_matches_known_set_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
    let dir = "/SAMPLES";
    let mut got: Vec<(String, bool, u32)> = e.readdir_all(dir);
    got.sort();
    assert_eq!(
        got,
        known_samples_dir_set(),
        "efatfs directory listing diverged from the known-good expected set"
    );
}

/// The fixture's `/SAMPLES` entries as sorted `(name, is_dir, size)`; directories have size 0.
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

/// The sample recorder's persistent write handle: unflushed writes leave the on-disk size stale,
/// yet read back through the write context; one flush then persists the full size.
#[test]
fn efatfs_noflush_write_then_read_via_context_before_flush_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
    let path = "/R3REC.BIN";
    const CB: usize = 64 * 512; // one 32KiB cluster (fixture geometry)
    let ctx = e.create_context(path, false);
    // Three distinct clusters, no flush between.
    let mut ctx = ctx;
    let mut clusters: Vec<Vec<u8>> = vec![];
    for c in 0..3u32 {
        let payload: Vec<u8> = (0..CB).map(|i| ((c as usize + i) & 0xff) as u8).collect();
        let (nc, w) = e.write_at_via_context_noflush(&ctx, c * CB as u32, &payload);
        assert_eq!(w, CB);
        ctx = nc;
        clusters.push(payload);
    }
    // The on-disk size is still stale.
    assert_eq!(
        EFatFs::mount().read_file(path).len(),
        0,
        "precondition: dir size not yet flushed"
    );
    // The write context's in-memory size covers what was written.
    let mut buf = vec![0u8; CB];
    let (nc, n) = e.read_at_via_context(&ctx, 0, &mut buf);
    ctx = nc;
    assert_eq!(n, CB);
    assert_eq!(&buf[..], &clusters[0][..], "mid-write read-back diverged");
    let _ctx = e.flush_context(&ctx);
    assert_eq!(
        EFatFs::mount().read_file(path).len(),
        3 * CB,
        "flush must persist the full size"
    );
}

/// The core's `readdir_open`/`readdir_next` list the known `/SAMPLES` entries.
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

// Recorder write paths. `deluge::io::Stream` reaches the core through the
// `deluge_efatfs_stream_*` C ABI, which composes the no-flush write, read-via-context and flush
// primitives these tests drive.

/// A recording-shaped sequential write, unflushed: every earlier cluster reads back exactly through
/// the open write context while the on-disk size is stale, and the flushed file round-trips
/// through a fresh mount.
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

    // The on-disk size is still stale.
    assert_eq!(
        EFatFs::mount().read_file(path).len(),
        0,
        "precondition: dir size not yet flushed"
    );

    // Every cluster, not just the latest, reads back through the unflushed context.
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

    // `Stream::close` flushes.
    let _ctx = e.flush_context(&ctx);

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

/// The recorder's finalize header patch: after an unflushed multi-cluster body, a write of the
/// first sector through the same open context lands, and the rest of the body is untouched.
///
/// `SampleRecorder::finalizeRecordedFile` patches the header before `Stream::close`, since
/// `write_at` needs an open handle.
#[test]
fn efatfs_write_at_header_patch_after_body_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
    let path = "/R3HDR.BIN";
    const CB: usize = 64 * 512; // one 32KiB cluster (fixture geometry)
    const NUM_CLUSTERS: u32 = 3;
    const SECTOR: usize = 512;

    // The recording body, as `SampleRecorder::writeCluster` writes it.
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

    // The header patch at offset 0, on the same context.
    let patched_header: Vec<u8> = (0..SECTOR).map(|i| 0xEEu8 ^ (i as u8)).collect();
    let (nc, w) = e.write_at_via_context_noflush(&ctx, 0, &patched_header);
    assert_eq!(
        w, SECTOR,
        "header-patch write must write the full first sector"
    );
    ctx = nc;

    // `Stream::close` flushes.
    let _ctx = e.flush_context(&ctx);

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

/// `SampleRecorder::alterFile`'s in-place rewrite: rewriting a middle cluster through a reopened,
/// non-truncating context changes only that cluster and not the file size.
#[test]
fn efatfs_write_at_middle_cluster_rewrite_fat32() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _disk = RamDisk::load(&fat32());
    let e = EFatFs::mount();
    let path = "/R3ALTER.BIN";
    const CB: usize = 64 * 512; // one 32KiB cluster (fixture geometry)
    const NUM_CLUSTERS: u32 = 4;
    const MIDDLE: u32 = 1; // neither the first nor the last cluster

    // The original recording, flushed.
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

    // `DELUGE_STREAM_WRITE_APPEND` reopens without truncating (`open_context`).
    let mut alter_ctx = e.open_context(path);

    let new_middle: Vec<u8> = (0..CB).map(|i| 0xC3u8 ^ (i as u8)).collect();
    let (nc, w) = e.write_at_via_context_noflush(&alter_ctx, MIDDLE * CB as u32, &new_middle);
    assert_eq!(w, CB, "in-place rewrite must write a whole cluster");
    alter_ctx = nc;

    let _alter_ctx = e.flush_context(&alter_ctx);

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
