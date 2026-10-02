# async-storage SP0: Rust-FS differential — go/no-go report

**Spike branch:** `feat/rustfs-sp0-differential` (off `feat/async-sd-owner-substrate` @ `e73335eaf`)
**Plan:** `docs/superpowers/plans/2026-07-20-async-storage-sp0-rustfs-differential.md`
**Design:** `docs/superpowers/specs/2026-07-20-async-storage-end-architecture-design.md`
**Date:** 2026-07-20

## What SP0 is

SP0 is the go/no-go gate for the whole async-storage migration: before spending
integration effort re-backing Deluge's file I/O onto an async Rust filesystem, prove
the candidate crate ([`embedded-fatfs`](https://github.com/MabezDev/embedded-fatfs), the
MabezDev async fork of `rafalh/rust-fatfs`) is *correct* against the C FatFS
(`src/fatfs`, chan R0.14b) Deluge ships today, and that it is *plausible* on the actual
device target. It does **not** integrate anything into the firmware — no fiber changes,
no `file_io.h`/`stream_io.h` wiring, no C FatFS deletion. That's SP1+.

The instrument is a host-only **differential test harness**
(`harness/rust/fs_differential/`): both filesystems mounted over the *same* in-RAM FAT
image, driven through a common `FsOps`/`FsOpsMut` trait, and diffed byte-for-byte on
content and entry-for-entry on directory-listing metadata. C FatFS is compiled for real
(via `cc`, from the actual vendored `src/fatfs/ff.c`/`ffconf.h`) — this is not a
reimplementation standing in as an oracle, it's the shipping code.

## Correctness

**Read + enumerate differential:** clean on both FAT16 and FAT32 fixtures. The whole
fixture tree (`fixtures/tree/`, including a multi-cluster LFN-named file) is walked
recursively through both backends; every directory listing and every file's bytes must
agree. Zero divergences on the corpus.

**Write differential** (create, multi-cluster write, cluster-boundary extend, rename,
delete), also clean on both FAT16 and FAT32, but only *after* two real bugs the
differential found in `embedded-fatfs` were fixed (below) — the harness first caught
both bugs red, then proved the fixes green.

**One documented normalization:** `embedded-fatfs`'s directory iterator yields `.`/`..`
pseudo-entries for non-root directories; C FatFS's `f_readdir` never does (it suppresses
them internally). This is an API-convention difference, not a data/metadata bug in
either library — the harness filters `.`/`..` out of `EFatFs::read_dir` so both backends
present the same logical directory view (`harness/rust/fs_differential/src/efatfs.rs`).

**Proven non-vacuous:** the differential was validated against itself via deliberate
fault injection (a 1-byte content mutation, an extra directory entry, a size-only
mutation) — all three were caught and reverted before trusting a "clean" result on the
real corpus.

### Two real write bugs found and fixed in the vendored crate

Both live in `crates/embedded-fatfs/src/dir.rs`; full before/after detail and the
regression proof are in `crates/embedded-fatfs/VENDOR.md`'s "Applied local fixes".

- **BUG-A — `Dir::rename` dropped a multi-component destination path.** `rename()`
  traversed `dst_path` into a local `e_dst`, but the traversal started from `self`
  instead of `dst_dir`, and the final call to `rename_internal` discarded `e_dst`
  entirely and passed the raw, untraversed `dst_dir` argument instead. Demonstrated:
  renaming `/REC/SHORT.RAW` to `/REC/Renamed Long.raw` landed the file at
  `/Renamed Long.raw` (root level) — the `"REC/"` destination component was silently
  dropped. Fixed: destination traversal now starts at `dst_dir`, and `rename_internal`
  is called with the traversed `e_dst`.
- **BUG-B — FAT32 `create_dir` wrote the wrong cluster into a new directory's `..`
  entry.** A subdirectory's `..` entry must carry first-cluster `0` when its parent is
  the volume root (FAT convention, followed by C FatFS) — not the root's own actual
  first-cluster number. FAT16/12 get this for free (their root has no real cluster
  number); FAT32's root is an ordinary cluster-backed directory, so `create_dir` wrote
  that real cluster in. Demonstrated with a raw on-disk probe (`fat32_dotdot_cluster_probe`
  in `tests/differential.rs`) reading the `..` entry's bytes directly, bypassing both
  backends' listing APIs (which both filter `.`/`..` and so never surfaced this):
  confirmed C FatFS writes cluster `0`, `embedded-fatfs` wrote cluster `2`
  (`BPB_RootClus`). Fixed by porting upstream `rafalh/rust-fatfs`'s `c4bb769`.

This divergence was invisible to the logical read/write differential itself (both
backends' listing APIs filter `.`/`..`) — it only surfaced because Task 6 added an
explicit raw-byte probe specifically to check for it, based on the upstream PR survey
flagging it as a known issue class. That's a real gap in "diff the logical tree" as a
complete correctness instrument, worth remembering for SP1/SP2: metadata fields that
never round-trip through either backend's own read API need their own explicit probes.

### Upstream fixes carried

Applied byte-faithfully to the vendored copy (each its own revertible commit — see
VENDOR.md):

- **PR #64** — FAT16 BPB `reserved_1` dirty-flag corruption fix (status-flag writes
  gated to FAT32 only) + `total_sectors_16`/`total_sectors_32` mutual-exclusivity
  validation + `NullTimeProvider` returning a valid date instead of an invalid
  all-zero one.
- **PR #55** — `FileSystem::new` actually seeks storage to offset 0 instead of only
  `debug_assert!`ing it's already there — a release-build silent-misparse-on-remount
  fix (the assert compiles out in release).
- **PR #59** — validates a resumed `FileContext` against the actual on-disk directory
  entry before trusting it, instead of only comparing in-memory state.

## Build

**Host, `no_std`:** clean. `crates/embedded-fatfs` is vendored (rev `518528c`) as its
own detached Cargo workspace (empty `[workspace]` table, so it can't perturb
`crates/Cargo.toml`'s workspace/lockfile that feeds the firmware build), with
`default-features = false, features = ["lfn", "alloc"]` — no `std`, no `chrono`, no
`log`. Builds clean under that config; this was already confirmed at Task 1/1B and
re-confirmed as part of this task's cross-compile check (same feature set).

**Device, `armv7a-none-eabihf` `no_std` cross-compile — the decisive device-readiness
finding:**

```
cd crates/embedded-fatfs
cargo build --release --target armv7a-none-eabihf -Zbuild-std=core --no-default-features --features lfn,alloc
```

This exact invocation (as specified) **fails**:

```
error[E0152]: duplicate lang item in crate `core` (which `alloc` depends on): `sized`
  = note: the lang item is first defined in crate `core` (which `embedded_fatfs` depends on)
  = note: first definition in `core` loaded from .../target/armv7a-none-eabihf/release/deps/libcore-....rmeta
  = note: second definition in `core` loaded from .../rustlib/armv7a-none-eabihf/lib/libcore-....rmeta
```

Root cause: the crate's `alloc` Cargo feature (`extern crate alloc`) needs the `alloc`
sysroot crate, but `-Zbuild-std=core` only rebuilds `core` from source — Cargo then
links the toolchain's *prebuilt* target-sysroot `alloc` (built against a *different*
prebuilt `core`) alongside the freshly-source-built `core`, and the two disagree on
lang items.

**Fix — rebuild `alloc` from source too:**

```
cargo build --release --target armv7a-none-eabihf -Zbuild-std=core,alloc --no-default-features --features lfn,alloc
```

This **builds clean** — one informational warning only
(`unstable feature specified for -Ctarget-feature: neon`, from
`crates/.cargo/config.toml`'s `[target.armv7a-none-eabihf]` rustflags, which set
`target-cpu=cortex-a9 target-feature=+neon` to match the firmware's own codegen ABI —
harmless and already tolerated by this repo's existing device Rust build). No other
errors, no other warnings from `embedded-fatfs` itself.

The `armv7a-none-eabihf` target and the nightly `rust-src` component are already
provisioned for this repo's Rust device build (`src/bsp/rust/rust-toolchain.toml`); no
new toolchain setup was needed beyond the `-Zbuild-std` flag correction above. That
correction is a one-line note for whoever wires the SP1 device build, not a blocker —
`-Zbuild-std=core,alloc` is a well-understood, standard invocation for any `no_std`
crate that uses `alloc`.

**Verdict:** the vendored, patched `embedded-fatfs` (core crate, our fixes included)
compiles clean for the actual device target with the actual device codegen flags. No
device-side architectural obstacle found.

## Throughput

**Host proxy (this task) — READ THE CAVEAT FIRST.** This measurement times both
backends over the **same in-RAM image** (`ram_disk.rs`'s shared `DISK` /
`efatfs.rs`'s `MemIo`) doing a 4 MiB contiguous write followed by a full sequential
read-back, on the FAT32 fixture (32 KiB clusters, matching the real target SD layout in
`src/bsp/rust/src/sd_image.rs`). **There is no SDHI controller, no DMA, no real
block-device command/response latency, no multi-block row-thrashing, and no card
erase-block/wear-leveling behavior anywhere in this path.** RAM access is ~1000x faster
than SD and has none of a card's access-pattern sensitivity. This measurement can only
see each stack's own **per-operation software overhead** (allocation, buffer copies,
FAT-chain walking, cluster-boundary bookkeeping) relative to the other — it is a
gross-overhead sanity check, **not a throughput-parity verdict**.

Test: `harness/rust/fs_differential/tests/differential.rs::throughput_proxy_fat32`. Run:

```
cd harness/rust/fs_differential
fixtures/mk_fixture.sh /tmp
SP0_FAT16=/tmp/fat16.img SP0_FAT32=/tmp/fat32.img cargo test --release throughput_proxy_fat32 -- --nocapture
```

Observed numbers (release build, matching how firmware Rust code actually ships; this
host, 2026-07-20, representative of several runs):

| | write | read |
|---|---|---|
| C FatFS | ~10,200–11,300 MB/s | ~4,900–5,300 MB/s |
| `embedded-fatfs` | ~8,500–9,500 MB/s | ~4,900–5,100 MB/s |

`embedded-fatfs` writes at roughly 75–85% of C FatFS's proxy rate and reads
essentially at parity (sometimes marginally faster) in a release build. No gross
per-operation overhead blow-up. (A debug build shows a much larger write gap —
`embedded-fatfs`'s unoptimized async state-machine codegen is markedly slower than
optimized — but that's a debug-build artifact of Rust `async`/`Future` polling, not
informative about device behavior, since firmware ships release builds.)

**The real number is deferred to SP1.** On-device sequential-read and contiguous-write
throughput, `embedded-fatfs` vs C FatFS, against the real `deluge_bsp::sd` block device
over actual SDHI/DMA, requires the `block-device-adapters` sector→byte bridge (not
vendored in SP0 — see VENDOR.md's "Deferred" section) plus Embassy device wiring —
both SP1 scope. **This is treated as a hardware gate on SP1/integration, not an SP0
correctness blocker**: SP0's job is proving the candidate is *correct* and *buildable
for the target*; whether it's *fast enough on real hardware* is answered once, on
device, before the migration proceeds past SP1.

## Deferred / skipped upstream fixes

Recorded in full in `crates/embedded-fatfs/VENDOR.md`; summary:

- **Deferred to SP1** (live in `block-device-adapters`, not vendored — SP0's `MemIo`
  harness implements `embedded-io-async` directly over the RAM image, bypassing that
  crate entirely):
  - **PR #68** — 32-bit `usize` overflow/truncation at ≥4 GiB in the buffering layer.
    A real *our-target* data-loss bug once on the sector-oriented device path — must be
    picked up when `block-device-adapters` is vendored for SP1.
  - **PR #62** — `BufStream`/`StreamSlice` seek sign/overflow bug, same crate, same
    deferral reason.
  - **PR #66** — rename `..`-update fix (moving a *directory*, not just a file, should
    update its own `..` entry to point at the new parent). Not exercised by SP0's write
    corpus (which renames a file); candidate for a hardening commit once a differential
    exercises a directory move.
- **Skipped (rejected, not deferred):**
  - **PR #32** — treats a corrupted LFN entry as a hard error aborting the whole
    directory listing. Rejected: cuts against this project's degrade-gracefully-on-
    power-loss posture.
  - **FSInfo / `format_volume` fix** (`2f1bf3e`) — Deluge never formats a card
    (`FF_USE_MKFS = 0`), so `format_volume` is unreachable for us.

## Decision

**GO.**

- Correctness: read+enumerate and write differentials are clean on both FAT16 and
  FAT32, proven non-vacuous by fault injection, with one documented (and justified)
  normalization. Two real bugs the differential found were fixed in the vendored crate,
  with RED-before-GREEN evidence; three relevant upstream hardening fixes are carried.
- Build: `no_std`-clean on host, and — after correcting the build-std invocation to
  include `alloc` — `no_std`-clean for the real device target (`armv7a-none-eabihf`)
  under the firmware's actual codegen flags (`cortex-a9`, `+neon`). No architectural
  device-side obstacle found.
- Throughput: the host proxy shows no gross per-operation overhead blow-up in a release
  build (write ~75–85% of C FatFS, read at parity). This is explicitly *not* a
  throughput-parity verdict — the real on-device number is deferred to SP1 as a
  hardware gate, evaluated before the migration proceeds past SP1, not as a condition
  for SP0 itself.

**Proceed to SP1** (device-integration scope: vendor `block-device-adapters`, pick up
PR #68/#62 fixes, wire the sector→byte bridge over the real `deluge_bsp::sd` device,
Embassy device wiring, and run the real on-device throughput measurement as the
hardware gate).

## Differential blind spots — SP1 must treat these as UNCOVERED, not validated

The differential compares a `.`/`..`-filtered, name-sorted logical tree of
`Entry { name, size, is_dir }` plus byte-exact file content. Anything outside that
surface is invisible to "differential clean." The whole-branch review named five
classes SP1's hardening differential must add explicit coverage for — do **not** assume
this harness proved them:

1. **Timestamps — actively diverge today.** C FatFS `get_fattime` returns 2024-01-01;
   embedded-fatfs's `NullTimeProvider` returns 1980-01-01. `Entry` carries no timestamp,
   so create/modify dates are silently uncompared (and any raw-image compare will differ).
2. **SFN aliases.** Only the long name is compared; the 8.3 short name / `~N` collision
   numbering (`altname`) is never checked — two implementations can generate different SFNs
   undetected.
3. **Attributes beyond `is_dir`** (read-only / hidden / system / archive) — uncompared.
4. **FAT-chain / free-space integrity — the most important uncovered class.** The logical
   tree cannot see leaked/orphaned clusters, wrong free counts, or an inconsistent FAT after
   delete/truncate. A delete that fails to free clusters looks perfectly clean. SP1 needs a
   lost-cluster scan or raw-FAT diff.
5. **Corpus gaps in the fixes themselves:** the non-root branch of the `..`-cluster fix,
   cross-directory rename (`self != dst_dir` — BUG-A's start-point half was verified only by
   upstream-match, not by the differential), and directory *move* (PR #66, deferred) are all
   unexercised. SP1's corpus should add nested `mkdir`, cross-dir rename, and directory move.

SP1's hardening differential should therefore add an SFN/attribute/timestamp-aware compare,
a FAT-chain/free-space integrity check, and the corpus ops above — plus the NEEDS-HARDWARE
gates (real SD throughput/underruns, power-loss safety) that no host harness can cover.
