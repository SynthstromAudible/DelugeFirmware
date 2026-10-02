# VENDOR.md — block-device-adapters

## Provenance

- **Source repo:** https://github.com/MabezDev/embedded-fatfs
- **Vendored rev:** `518528cc111fcf65c48abbdeb80735a38eada112` (short: `518528c`), same rev already
  vendored for `crates/embedded-fatfs` (SP0) and `crates/block-device-driver` (this task).
- **Vendored subtree:** the whole `block-device-adapters/` crate from the upstream workspace —
  `Cargo.toml`, `src/lib.rs`, `src/buf_stream.rs`, `src/stream_slice.rs`, `src/fmt.rs`,
  `LICENSE-APACHE`, `LICENSE-MIT`, `README.md`. This crate provides `BufStream` (byte-level
  `embedded-io-async::{Read,Write,Seek}` over a block-aligned `BlockDevice`) and `StreamSlice`
  (partition/offset windowing over a stream) — the device-bridge layer between our SD-card block
  device and `embedded-fatfs`.
- **License:** dual MIT/Apache-2.0 upstream (`Cargo.toml` says `license = "MIT"`; both `LICENSE-MIT`
  and `LICENSE-APACHE` files are present upstream and preserved verbatim here). Compatible with this
  repo's GPL-3.0.
- **Dependencies:**
  - `aligned = "0.4.2"` — crates.io, kept as-is (not vendored).
  - `embedded-io-async = "0.7.0"` — crates.io, kept as-is (not vendored).
  - `block-device-driver = { version = "0.2", path = "../block-device-driver" }` — repointed at the
    sibling vendored crate `crates/block-device-driver` (path unchanged from upstream, since upstream
    already used a sibling-relative path within its own workspace and our vendored layout mirrors that
    sibling relationship).
  - `log`/`defmt` optional deps and the `tokio`/`env_logger`/`anyhow`/`embedded-io-adapters`
    dev-dependencies are left as declared upstream (crates.io, only pulled in for the crate's own
    `cfg(test)` unit tests, which are vendored in `src/*.rs` as inline `mod tests`).
- **Workspace isolation:** `crates/block-device-adapters/Cargo.toml` carries an empty `[workspace]`
  table so this directory is its own detached workspace root — it does not attach to (and cannot
  perturb) `crates/Cargo.toml`'s workspace/lockfile, which feeds the firmware build. It is
  intentionally NOT listed in `crates/Cargo.toml`'s `members`.
- **Not vendored:** upstream `examples/` (none present) — this crate has no separate `tests/`
  directory either; its tests live inline in `src/buf_stream.rs`/`src/stream_slice.rs` as `mod tests`,
  which are vendored along with the rest of those files.

## Why own the code instead of a git dependency

Same rationale as `crates/embedded-fatfs` (see that crate's `VENDOR.md`): this crate is unpublished on
crates.io (only available via the upstream git checkout), so we vendor it as an owned path dependency
to carry our own bug-fix and hardening commits without depending on an upstream repo we don't control.
SP0's `VENDOR.md` explicitly deferred PR #68 and #62 to this task (the block-device-adapters path
wasn't vendored yet in SP0 — its `MemIo` test harness implemented `embedded-io-async` directly over a
RAM image, bypassing this crate). Both are applied below.

## Applied upstream PR fixes (each its own commit, individually revertible)

- **PR #68** — two related fixes in `BufStream` (`src/buf_stream.rs`) and `StreamSlice`
  (`src/stream_slice.rs`):
  1. Zero-length `read`/`write` calls are now a no-op (`Ok(0)`, no device transfer). Previously a
     zero-length buffer would satisfy the block-aligned fast path and issue a 0-block transfer to the
     underlying device — some real hardware (e.g. STM32 SDMMC DMA) rejects a 0-block transfer outright.
  2. `StreamSlice::read`/`write` now compute `remaining = self.size - self.current_offset` in `u64`
     and narrow to `usize` only *after* `cmp::min` against the requested length, instead of narrowing
     `remaining` to `usize` first. On a 32-bit target (our `armv7a-none-eabihf` device build,
     `usize == u32`), a `remaining` value that's an exact multiple of 4 GiB (or otherwise ≥ 2^32)
     previously truncated to 0 via `as usize`, silently reporting a zero-length available read/write
     instead of the real remaining size. Applied cleanly with `git apply -p2
     --directory=crates/block-device-adapters --3way` (no conflicts; test hunks vendored as-is since
     both files' tests are inline `mod tests` and both files are fully vendored).
- **PR #62** — `BufStream::seek`/`StreamSlice::seek` sign/overflow hardening
  (`src/buf_stream.rs`, `src/stream_slice.rs`): `SeekFrom::Current`/`SeekFrom::End` arithmetic
  previously did a raw `as i64 + x` / `as i64 - x` that could overflow or, for `Current`, silently
  wrap `self.current_offset` through `i64` without checking. `BufStream::seek` now uses
  `checked_add`/`.max(0)` to clamp instead of wrapping, and `StreamSlice::seek` now uses
  `i64::try_from` + `checked_add`, returning `StreamSliceError::InvalidSeek` on overflow instead of
  silently producing a garbage offset. Applied cleanly with `git apply -p2
  --directory=crates/block-device-adapters --3way` (no conflicts — this PR's hunks touch the `Seek`
  impls, disjoint from PR #68's `Read`/`Write` hunks, despite pr_62.diff's base blob SHAs for
  `stream_slice.rs` predating pr_68.diff's post-fix SHA; `git apply` context-matches rather than
  requiring exact blob equality, and both applied without fuzz).

## Build verification

- Host (`cargo build`, default target): clean, `no_std`-compatible crate builds fine on host too
  (see report for exact command/output).
- Device (`cargo build --release --target armv7a-none-eabihf -Zbuild-std=core,alloc`): see report for
  exact command/output.
