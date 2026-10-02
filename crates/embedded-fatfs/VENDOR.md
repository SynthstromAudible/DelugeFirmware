# VENDOR.md — embedded-fatfs

## Provenance

- **Source repo:** https://github.com/MabezDev/embedded-fatfs
- **Vendored rev:** `518528cc111fcf65c48abbdeb80735a38eada112` (short: `518528c`), dated 2026-05-01
- **Vendored subtree:** the core crate only — `embedded-fatfs/Cargo.toml`, `embedded-fatfs/src/`,
  `embedded-fatfs/LICENSE`, `embedded-fatfs/README.md` from the upstream workspace. The upstream
  workspace also contains `block-device-adapters`, `block-device-driver`, `embedded-partitions`, and
  `sdspi` — none of those are vendored here; the core crate's only dependencies are the published
  crates.io crates `bitflags = "1.0"` and `embedded-io-async = "0.7.0"`, kept as normal crates.io deps
  (not vendored).
- **License:** MIT (upstream `license = "MIT"` in `Cargo.toml`; `LICENSE` file preserved verbatim,
  copyright held by Rafał Harabień 2017 and Scott Mabin 2023). Compatible with this repo's GPL-3.0.
- **Workspace isolation:** `crates/embedded-fatfs/Cargo.toml` carries an empty `[workspace]` table so
  this directory is its own detached workspace root — it does not attach to (and cannot perturb)
  `crates/Cargo.toml`'s workspace/lockfile, which feeds the firmware build. It is intentionally NOT
  listed in `crates/Cargo.toml`'s `members`.
- **Not vendored:** upstream `tests/`, `examples/`, `resources/`, `scripts/`, `CHANGELOG.md`,
  `rustfmt.toml` — not needed to build the library; the `resources/*` exclude in `Cargo.toml` is
  inherited from upstream and left as-is (harmless, since those files aren't present here either).

## Why own the code instead of a git dependency

`embedded-fatfs` is unpublished (crates.io has no release); the prior task git-pinned it
(`rev = 518528c`) and confirmed it builds `no_std`-clean. This task replaces that git dependency with
a vendored path dependency so we can carry our own bug-fix and hardening commits without depending on
an upstream repo we don't control. Divergences this project's differential-testing work finds become
hardening commits in this owned copy.

## Applied upstream PR fixes (each its own commit, individually revertible)

- **PR #64** — FAT16 BPB `reserved_1` dirty-flag corruption fix (status flags/dirty-flag writes are
  gated to FAT32 only; FAT12/16 do not carry status flags in the BPB) + `total_sectors_16`/
  `total_sectors_32` mutual-exclusivity validation fix + `NullTimeProvider` now returns a valid
  1980-01-01 date/time instead of decoding an all-zero (invalid) date. Applied to `src/boot_sector.rs`,
  `src/fs.rs`, `src/time.rs` (the `tests/write.rs` hunk in the upstream PR was not applied — `tests/`
  is not vendored).
- **PR #55** — `FileSystem::new` now actually seeks the storage to offset 0 (`SeekFrom::Start(0)`)
  instead of only asserting the position is already 0 via `SeekFrom::Current(0)`. Release builds
  (where `debug_assert!` is compiled out) previously silently misparsed a remount from a
  non-zero/stale cursor. Applied to `src/fs.rs`.
- **PR #59** — validates a resumed `FileContext` against the actual on-disk directory-entry bytes
  before trusting it (`File::new_from_context` now re-reads the on-disk entry and compares it to the
  context's cached entry, returning `Error::InvalidInput` on mismatch), rather than only comparing the
  in-memory `DirEntryEditor`. Adds `DirFileEntryData::to_bytes()` and `DirEntryEditor::pos()` helpers.
  `to_file_with_context`/`try_to_file_with_context` become `async` to perform the disk check. Applied
  to `src/dir_entry.rs`, `src/file.rs` (the `tests/read.rs` hunk in the upstream PR was not applied —
  `tests/` is not vendored). Supersedes PR #46, which is not applied.

## Deferred (SP1 — block-device-adapters path, not vendored yet)

- **PR #68** — 32-bit `usize` overflow/truncation at ≥4 GiB in the block-device-adapters buffering
  layer. A real *our-target* data-loss bug once we're on the sector-oriented device path, but that
  crate isn't vendored in SP0 (SP0's `MemIo` harness implements `embedded-io-async` traits directly
  over a RAM image, bypassing block-device-adapters entirely).
- **PR #62** — `BufStream`/`StreamSlice` seek sign/overflow bug, also in block-device-adapters.
  Deferred alongside #68 for the same reason.

## Applied local fixes (beyond upstream, evidence-driven hand-ports)

- **BUG-A — `Dir::rename` silently dropped a multi-component `dst_path`** (found via Task 6's write
  differential, 2026-07-20; fixed Task 6B, `crates/embedded-fatfs/src/dir.rs`'s `rename`). `rename()`
  traverses `dst_path` relative to `dst_dir` into a local `e_dst`, but the destination traversal loop
  started from `self` instead of `dst_dir`, and the final call to `rename_internal` then discarded
  `e_dst` entirely and passed the raw, untraversed `dst_dir` parameter instead. Both only happened to
  cancel out when a caller passed the same directory as `self` and `dst_dir` (this fn's own "no moving"
  case). **Demonstrated (Task 6):** `root.rename("REC/SHORT.RAW", &root, "REC/Renamed Long.raw")` renamed
  the file to `/Renamed Long.raw` (root level), not `/REC/Renamed Long.raw` — the `"REC/"` destination
  component was silently dropped. Task 6 worked around this in the differential harness
  (`fs_differential/src/efatfs.rs::EFatFs::rename`) by resolving both parent directories itself and
  calling `Dir::rename` with leaf-only names, never reaching the buggy path. **Fixed (Task 6B):** the
  destination traversal now starts at `dst_dir` and `rename_internal` is called with the traversed
  `e_dst`, not the raw `dst_dir` parameter. The harness workaround was removed — `EFatFs::rename` now
  calls `Dir::rename` directly — and `write_diff_fat32`/`write_diff_fat16`
  (`tests/component/differential/fs_differential/tests/differential.rs`) are the regression proof, driving the real fix
  through a multi-component rename destination.
- **BUG-B — FAT32 `..`-cluster-zero** (upstream `rafalh/rust-fatfs` commit `c4bb769`, "Fix .. cluster
  number for first-level dirs"; found via Task 6's `fat32_dotdot_cluster_probe`, 2026-07-20; fixed Task
  6B, `crates/embedded-fatfs/src/dir.rs`'s `create_dir` + `src/file.rs`). A new subdirectory's `..` entry
  must carry first-cluster `0` when its parent is the volume root (the FAT spec's convention, followed by
  C FatFS), not the root's own actual first-cluster number. FAT12/16 already got this for free — their
  root is the dedicated `DirRawStream::Root` region, whose `first_cluster()` is always `None` — but
  FAT32's root is an ordinary `File`-backed directory with a real cluster number (`FileSystem::root_dir`
  gives it a `DirRawStream::File` wrapping `File::new(Some(bpb.root_dir_first_cluster), None, ..)`), so
  `create_dir` wrote that real cluster (`e.stream.first_cluster()`) into `..` instead. **Demonstrated
  (Task 6):** `fat32_dotdot_cluster_probe` created a directory directly under a FAT32 root through both
  backends and read the raw 32-byte `..` directory entry (both backends' `read_dir` filter `.`/`..` out,
  so this needs a raw byte read to see); confirmed C FatFS writes cluster **0**, embedded-fatfs writes
  cluster **2** (the root's own actual first cluster, read from the same image's BPB `BPB_RootClus`).
  **Fixed (Task 6B):** ported `c4bb769` — added `is_root_dir()` on `DirRawStream` (`dir.rs`) and `File`
  (`file.rs`, `self.context.entry.is_none()` — the root's `File` is the only one built with no owning
  `DirEntryEditor`), and `create_dir` now writes `None` (serializes to cluster 0) for the `..` entry
  when the parent is the root. `fat32_dotdot_cluster_probe` now asserts the two backends' `..`
  first-cluster fields are EQUAL (both 0) — flipped from its Task 6 form, which asserted the known
  divergence — and is the regression proof.
- **PR #66** — rename `..`-update fix (when *moving a directory*, not just a file, its own `..` entry
  should be updated to point at the new parent). Deferred: shares the same root-cluster caveat as BUG-B
  above (evidence-driven — Task 6's write corpus renames a *file*, not a directory, so this path wasn't
  exercised/confirmed) and is a distinct bug from BUG-A (BUG-A is about `rename_internal`'s destination
  *parent* resolution; PR #66 is about updating the *moved directory's own* `..` entry after the move
  completes). Not applied — candidate for a future hardening commit once a differential exercises a
  directory rename/move.
- **SP1b-1 (local, upstreamable): forward-continue in `Seek for File`.** `src/file.rs`.
  Upstream re-walks the cluster chain from `first_cluster` on every cross-cluster
  seek — only a same-cluster fast path exists. This continues from
  `current_cluster` when the target is ahead, turning an O(n) walk into O(delta).
  Pure optimization; no API change.

## Skipped (rejected, not deferred)

- **PR #32** — treats a corrupted LFN entry as a hard error that aborts the whole directory listing.
  Rejected: cuts against this project's degrade-gracefully-on-power-loss posture (we'd rather list what
  we can than fail the whole directory over one bad LFN chain).
- **FSInfo / `format_volume` fix** (upstream commit `2f1bf3e`) — Deluge never formats a card
  (`FF_USE_MKFS = 0` in the existing FatFs config), so `format_volume` is unreachable code for us; not
  worth carrying the fix.
