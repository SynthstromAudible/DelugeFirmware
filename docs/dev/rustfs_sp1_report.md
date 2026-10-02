# async-storage SP1: device block-device bridge — report

**Branch:** `feat/rustfs-sp1-device-bridge` (off `feat/async-sd-owner-substrate` @ SP0 merge)
**Prior report:** `docs/dev/rustfs_sp0_report.md` (host-only correctness/build go/no-go)
**Date:** 2026-07-20

## What SP1 is

SP0 proved `embedded-fatfs` is correct against C FatFS and compiles for the device
target, using a host-only harness (`MemIo`, an `embedded-io-async` shim implemented
directly over a RAM image) that bypassed the real device storage stack entirely. SP1
closes that gap: stand up the actual sector-oriented bridge — `block-device-driver` +
`block-device-adapters`' `BufStream` — between `embedded-fatfs` and a real
`BlockDevice`, re-point SP0's whole differential through it on host (de-risking the
bridge itself before touching hardware), then build the device-side `BlockDevice` over
the real `deluge_bsp::sd` driver and prove the whole stack cross-compiles and links for
`armv7a-none-eabihf`. It closes with an on-device read-throughput benchmark structure —
the number itself is a hardware gate, run separately.

SP1 does **not** integrate anything into firmware I/O — no fiber changes, no
`file_io.h`/`stream_io.h` wiring, no C FatFS deletion, no live streaming-loader path.
That's SP2+.

## Vendoring: block-device-driver + block-device-adapters

Vendored at the same upstream rev already used for `crates/embedded-fatfs` (SP0),
`518528c`, as two more detached-workspace crates (`crates/block-device-driver`,
`crates/block-device-adapters`) — not in `crates/Cargo.toml`'s members, firmware
`Cargo.lock` untouched. `block-device-driver` is vendored byte-identical to upstream.
`block-device-adapters` carries two applied upstream fixes to `BufStream`/`StreamSlice`
(full detail in `crates/block-device-adapters/VENDOR.md`):

- **PR #68** — zero-length `read`/`write` calls become a no-op instead of issuing a
  0-block device transfer (some real hardware, e.g. STM32 SDMMC DMA, rejects those
  outright); and a 32-bit `usize` truncation fix — `remaining` was narrowed from `u64`
  to `usize` *before* clamping against the requested length, so on our 32-bit
  `armv7a-none-eabihf` target (`usize == u32`) a remaining size that was an exact
  multiple of 4 GiB or larger silently truncated to zero. SP0 flagged this as a real
  our-target bug and deferred it here, since `block-device-adapters` wasn't vendored
  yet. Only inspection-validated in SP1 — there is no ≥4 GiB fixture in the corpus to
  exercise it against.
- **PR #62** — `BufStream::seek`/`StreamSlice::seek` sign/overflow hardening: raw
  `as i64 ± x` arithmetic that could overflow or silently wrap is replaced with
  `checked_add`/clamping, returning an explicit seek error instead of a garbage offset.

Both applied clean, no conflicts.

## Correctness: the real stack, on host

Task 2's de-risk payload: a host `FileBlockDevice` (`block_device_driver::BlockDevice<512>`
over the existing RAM-disk `read_at`/`write_at`/`len`), and `EFatFs::mount` re-pointed
from SP0's `MemIo` shortcut to `block_device_adapters::BufStream::<FileBlockDevice, 512>`
— the same adapter stack, `#68`/`#62` fixes included, SP1 drives on device. `MemIo` and
its `embedded-io-async` impls are deleted.

The **entire SP0 differential corpus** — read/write diffs on both FAT16 and FAT32, the
FAT32 `..`-cluster raw probe, the throughput proxy, 8 `#[test]`s total — passes green
through the real `BlockDevice`→`BufStream`→`embedded-fatfs` stack, first try, stable
across repeated runs. No adapter-layer findings: `#68`/`#62` behave identically to the
`MemIo` baseline, bit-for-bit. Block-address arithmetic is exercised multi-block (64
contiguous 512-byte blocks in a single write), `size()` matches the disk length
correctly, and `embedded-fatfs` already flushes `BufStream`'s dirty block at the end of
every mutating operation, so no extra flush wiring was needed.

This means the on-host correctness result from SP0 (clean differential, two real bugs
found and fixed in `embedded-fatfs` itself, three upstream hardening fixes carried) now
holds through the actual code path the device build uses, not just a stand-in — the one
gap it can't close from host is that the underlying bytes are RAM, not a real SD card.

## Device readiness: the real stack, cross-compiled and linked

Task 3 built `SdBlockDevice` (`src/bsp/rust/src/fat_block_device.rs`), the device
counterpart of `FileBlockDevice`, wrapping `deluge_bsp::sd::{read_sectors,write_sectors,
total_sectors}` as a `block_device_driver::BlockDevice<512>`. Unlike the host adapter,
`type Error = SdError`, not `Infallible` — real SDHI/DMA I/O can genuinely fail (card
removed, protocol error, DMA timeout), and `SdError` keeps that visible to
`embedded-fatfs` instead of discarding it. `read`/`write` reinterpret the
`Aligned<A4, [u8; 512]>` block slice as flat bytes via `from_raw_parts(_mut)`, justified
by `Aligned<A, T>`'s `#[repr(C)]` layout over `T` plus a zero-sized alignment marker
(documented inline with `SAFETY` comments; reviewed and confirmed sound).

The build result is the decisive device-readiness finding: a whole-stack
`armv7a-none-eabihf` cross-compile — `cargo build -Zbuild-std=core,alloc` — produced a
**genuinely linked ARM ELF**, archived into the 396-object CMake `deluge_app`, not a
check-only `cargo check`. A `#[allow(dead_code)]` stack-instantiation smoke function
(`SdBlockDevice` → `BufStream` → `embedded_fatfs::FileSystem`) forces the type-checker
to fully monomorphize the whole chain for the real target, so the cross-compile result
means what it claims.

## Significant finding: `embedded-fatfs` is not alloc-free on device

`embedded-fatfs`'s `alloc` feature turned out to be the **first BSP code that needs a
heap on device** — no `#[global_allocator]` existed before this. Task 3 wired
`deluge_alloc::DelugeGlobalAlloc` as `FS_ALLOCATOR` in `main.rs`, left **uninitialized**
(safe by construction: an allocation through a null handle returns null rather than
invoking UB, so nothing breaks at build or link time) — reviewed and confirmed sound.
Task 4's benchmark is the first thing that actually needs `alloc` (LFN directory-scan
scratch, path strings), so it owns the one-time `.init()` over a 96 KiB static arena
before mounting.

**This is a real concern for SP2, not just an implementation detail:** heap allocation
on the audio hot-read path is a real-time hazard. SP2, which routes the live streaming
fill through this stack, must check whether `embedded-fatfs` allocates per-read in
steady state (not just at mount/directory-scan time) and, if so, either find or build an
alloc-free read path (preallocated handles, a bounded arena reused per read) rather than
accepting a general-purpose allocator call on every audio-driven read.

## Significant finding: `SdBlockDevice` bypasses `SD_BUS` arbitration

`SdBlockDevice`'s reads go straight to `deluge_bsp::sd::read_sectors`, which does **not**
take the `SD_BUS` lock the existing C `diskio` layer uses to arbitrate SD-card access.
Harmless in SP1's own contexts (device cross-compile is build-only; the benchmark's own
reads run at boot, before the C++ app or its own SD access is live, so nothing else is
touching the card). But this **will** race real audio-driven SD access once something
concurrent exists. **SP2 must route `SdBlockDevice` through the same `SD_BUS`
arbitration the C diskio layer uses** before it runs alongside the audio path.

## Significant finding (hardware): real cards are MBR-partitioned — `embedded-fatfs` needs the offset

The single most important thing the on-device run caught, invisible to every host test:
**a real Deluge SD card is MBR-partitioned.** Sector 0 is the partition table (`0xAA55`
signature, jump bytes `00 00 00` — *not* a FAT `EB`/`E9` boot-sector jump), and the FAT
volume's boot sector lives at the partition's start LBA (2048 on the test card). Mounting
`embedded-fatfs` directly over the whole `SdBlockDevice` made it read the MBR as a FAT VBR
and fail with `CorruptedFileSystem`. **C FatFS handles this transparently** (its `f_mount`
auto-detects MBR vs VBR and follows the partition); embedded-fatfs does not — the caller
must give it the offset.

This was never exercised on host because the `fs_differential` mtools fixtures are
partitionless **superfloppies** (`mformat` writes the FAT VBR at sector 0). It's a
textbook fixture-vs-real-card blind spot — precisely the class SP0's report warned SP1/SP2
not to assume the host harness covered.

- **Fix (prototyped in `bench_fs`, confirmed on hardware):** read sector 0, and if it's an
  MBR, mount over a `block_device_adapters::StreamSlice` of the first partition
  (`start_lba·512 .. (start_lba+nsec)·512`); fall back to whole-device for a superfloppy.
  With that, embedded-fatfs mounts the real card and reads it at parity (see Throughput).
- **SP2 requirement:** the device FS mount must go through MBR/partition resolution —
  `StreamSlice` as above, or `embedded-partitions` (in the same vendored workspace) for
  general MBR parsing — before `FileSystem::new`. **SP2's host differential must also gain
  a partitioned (MBR) fixture**, not just superfloppies, so this can't regress unseen.
- The card itself was verified healthy (`fsck.fat`: 42 files, clusters consistent) — the
  `CorruptedFileSystem` error was embedded-fatfs mis-reading the MBR, not real damage.

## Significant finding (hardware): `log::` is release-stripped; benchmark output uses `rprintln!`

`rza1l-hal` and `deluge-bsp` both declare `log = { features = ["release_max_level_off"] }`.
Cargo's feature unification makes that **additive across the whole graph**, so in any
`--release` build **every `log::` macro (down to `error!`) is compiled to nothing** —
confirmed empirically (the release ELF contains none of the log strings; the debug ELF
does). This is deliberate (strip logging from release firmware), and `log`→RTT works
normally in *debug* builds. But the benchmark **must** run in release for a fair number (a
debug build would make Rust `embedded-fatfs` artificially slow versus always-optimized C
FatFS), so `bench_fs`'s own diagnostics use `rtt_target::rprintln!` — which is not a `log`
macro and survives release. (Note: this rtt-target's `rprintln!` does *not* support inline
capture `{x}`; the benchmark uses explicit positional args.)

## Throughput

Task 4 built the on-device benchmark structure (`src/bsp/rust/src/bench_fs.rs`, gated
behind the non-default `bench_fs` Cargo feature so a normal firmware build never links
it). It initializes `FS_ALLOCATOR` over a 96 KiB arena, scans `SAMPLES/` (or root) for
the largest file, and times a sequential read of it two ways — first through
`embedded-fatfs` over `BufStream<SdBlockDevice, 512>`, then, after dropping that mount,
through the raw C FatFS `f_open`/`f_read` ABI against the same vendored `src/fatfs`
library the C++ app links, with an explicit `f_mount(null, ...)` unmount after each side
so the two never collide or leave a dangling `FATFS*` in FatFs's single mount table.
Both paths ultimately bottom out at the same `deluge_bsp::sd` SDHI/DMA driver, so the
comparison isolates the two filesystem implementations' overhead, not the hardware
underneath them. It logs a single result line:

```
SP1_BENCH efatfs read=… MB/s ; cfatfs read=… MB/s
```

C-side struct sizes (`FATFS`/`FIL`) were **measured, not guessed**: the device build
embeds a `FF_CACHE_ALIGN(32)` DMA-aligned window absent on host, so a real
`sizeof`/`alignof` probe was cross-compiled against the actual `ff.h` with this repo's
own `arm-none-eabi-gcc` under the firmware's real `ARCH_FLAGS`, yielding exact
`FATFS = 640` bytes / `FIL = 576` bytes, both 32-byte aligned. Reviewed independently and
confirmed against both `ffconf.h` variants. (One reviewer polish item is carried
forward, not yet applied: these exact sizes have no compile-time guard or safety
margin — a future `ffconf.h` edit without re-probing would silently corrupt the C-side
struct layout. `fs_differential`'s host harness over-sizes its equivalent guard structs
for exactly this reason; `bench_fs`'s device structs should get the same margin or an
explicit `const_assert!` before this ships past SP1.)

Cross-compiles clean for `armv7a-none-eabihf`, both with `--features bench_fs` and with
default features (i.e., the feature gate genuinely keeps this out of normal builds);
`crates/Cargo.lock` untouched.

**On-device result (measured 2026-07-20, real Deluge over J-Link):**

```
bench_fs: scanned 13 dirs / 29 files; largest = APPS/community (65,447,824 bytes)
SP1_BENCH efatfs read=3.83 MB/s ; cfatfs read=3.84 MB/s
```

Reading a **62 MB** file off the real SD/SDHI/DMA path: **embedded-fatfs 3.83 MB/s vs
C FatFS 3.84 MB/s — 99.7%, indistinguishable. The throughput-parity gate PASSES.** The
single-block-`BufStream` overhead the host proxy inflated to ~38% (below) is **negligible
on real hardware**: the SD transfer latency dominates and swamps the per-op CPU
difference — exactly the "not SD-representative" caveat the proxy carried. No SP2
cache-enlargement is needed for read parity; keeping the cluster→sector map is now an
optional optimization, not a fix.

Getting that number required three fixes on top of the Task-4 build (all landed in
`bench_fs.rs`, and each is itself a hardware finding — see below): output via
`rtt_target::rprintln!` (not `log::`, which release-strips), a partition-aware
`StreamSlice` mount, and a recursive file finder (this card's samples are nested, not in
`SAMPLES/` top level).

**Methodology caveats for reading that run:**

- The two reads are always ordered efatfs-then-cfatfs, never alternated or repeated.
  If SD cards exhibit any read-caching behavior, the second (C) read could be
  systematically favored. Worth a second look if the two numbers come back
  suspiciously close.
- The printed "MB/s" is **binary** MiB/s (bytes ÷ 2²⁰ ÷ seconds), not decimal
  (bytes ÷ 10⁶) — matches storage-throughput convention but is worth stating plainly
  when quoting the number elsewhere.
- Both reads use the same 4 KiB, block-aligned chunk size and the same target file, so
  this compares implementation overhead on an otherwise-identical access pattern, not
  cache-tuning of either side.

**A signal from the host-proxy result (Task 2, not device-representative):** measuring
`embedded-fatfs` through the real `BufStream` adapter (instead of SP0's `MemIo` shortcut)
over the same in-RAM image dropped its write throughput to roughly **38% of C FatFS**
(down from SP0's ~75–85% measured against `MemIo` directly). That drop is attributable
to `BufStream` itself, which caches exactly **one 512-byte block** internally — no
multi-block readahead or write-coalescing. This is a RAM-timing proxy, not an SD
prediction (SP0's report already establishes RAM access has none of a card's
access-pattern sensitivity), but it's the clearest available signal for *where* a
device-side shortfall would likely come from, if one shows up: the single-block
`BufStream` cache is the first thing to look at, and enlarging it (or, longer-term,
keeping the cluster→sector map itself as SP2 already has reason to touch for the
streaming-fill integration) is the natural lever.

## Conclusion

**Device bridge PROVEN in software AND on hardware; throughput at parity.** (On-device run
2026-07-20 closed the one gate SP1 had deferred — embedded-fatfs reads the real card at
3.83 MB/s vs C FatFS 3.84 MB/s, after the MBR-partition fix below.)

- **Correctness:** the full SP0 differential (8 tests, both FAT variants, read+write,
  the FAT32 `..`-cluster probe) now runs green through the *real*
  `BlockDevice`→`BufStream`→`embedded-fatfs` stack, not a stand-in — first try, stable,
  no adapter-layer findings. The one piece correctness coverage can't reach from host is
  the actual SD card underneath; that's unavoidable without hardware and isn't a
  software concern.
- **Device readiness:** the whole storage stack — `SdBlockDevice` over the real
  `deluge_bsp::sd` driver, through `BufStream`, into `embedded-fatfs` — cross-compiles
  and links into a genuine `armv7a-none-eabihf` ELF, archived into the real
  `deluge_app` build. No architectural device-side obstacle found.
- **Two real concerns surfaced, both scoped to SP2, not SP1:** `embedded-fatfs` needs a
  heap on device (first BSP alloc user — a real-time hazard on an audio-adjacent hot
  path if not checked/mitigated), and `SdBlockDevice` bypasses the `SD_BUS` arbitration
  lock the existing C diskio layer uses (harmless today, a real race once run alongside
  audio). Both are flagged, not fixed here — SP1 has no concurrent audio path to race
  against yet.
- **Throughput:** the benchmark is built, wired correctly (fair, sequential, same
  chunk/file, isolates FS-implementation overhead from hardware), and cross-compiles for
  device — but the number that matters is Kate's on-device `SP1_BENCH` run, not
  anything measurable from host. The host-proxy result (~38% of C FatFS through the real
  `BufStream` adapter) points at the single-block `BufStream` cache as the likely lever
  if the device number comes back short, but is explicitly not a prediction of it.

This **unblocks SP2**: routing the live streaming-fill task through this same
`BlockDevice`→`BufStream`→`embedded-fatfs` stack, golden-gated against the existing
firmware behavior, and required to address both the heap-on-hot-path and
`SD_BUS`-arbitration concerns raised here before it can run concurrently with audio.
