# SP1a — live streaming-read integration (embedded-fatfs) — report

**Branch:** `feat/rustfs-sp1a-streaming-read` (off `feat/async-sd-owner-substrate` @ `2f2c19ad5`)
**Status:** software-complete + host-verified; **on-device flag-on exercise owed (Kate's hardware gate)**.
**Feature:** cargo `efatfs_streaming`, **default OFF**. Flag off ⇒ byte-identical to today by construction.

## What SP1a does

Routes the **async streaming-fill task's read** through a per-stream vendored-`embedded-fatfs` file handle
instead of the raw cluster→sector-map read, flag-gated. This is the first *live, golden-gated* use of the
Rust FS. It removes **zero** C-FatFS consumers — the sync-fiber streaming fallback, the `SampleCluster::sdAddress`
map, and the open-time FAT walk all stay as the flag-off path. Retiring the map is the paired follow-on **SP1b**.

## The pieces (commits on this branch)

| Area | What | Commit |
|---|---|---|
| Ladder doc | Renumber SP rungs to as-built (SP0 → SP-bridge → SP-stream-read → …) | `f10dbe118` |
| Mount | `efatfs_fs.rs`: one `FileSystem` behind an async `Mutex`, partition-aware mount | `a4bc5b987` |
| Handle table | `open`/`read_at`/`close` over `FileContext` detach/reattach; generation guard | `4ef3f08e5`, `9e4c174ed` |
| FFI + descriptor | `deluge_efatfs_open/_close` (block_on_fiber bridge); descriptor `handle`/`byte_offset` | `2217d820f` |
| Read swap | `ProdOps::read` → `efatfs_core::read_at` when `handle != 0` (compile-gated) | `03f262bfb` |
| Live wiring | boot-mount + arena; `SampleStream` opens/closes a handle | `15eaa4280` |
| Host-testable core | extract storage-generic `efatfs_core` from device `efatfs_fs` | `5af30f2a2` |
| Margin proxy | Lens-1 SD-transfer-overhead measurement | `e9331aafb` |

## Architecture notes worth keeping

- **Concurrency:** embedded-fatfs's disk is a non-reentrant `RefCell`; every FS op serializes through one async
  `Mutex` (`with_fs`). The handle table is a second `Mutex`. **Lock order is never nested** — `read_at`
  checkouts the context out of the table, *releases* it, does the FS work under `with_fs`, then re-locks to
  commit (generation-gated). `open` takes FS→table; `read_at` takes table→FS-with-work-outside-the-table-lock,
  so the two never hold both and can't deadlock.
- **Generation guard:** a `close`+`open` recycle of a slot index racing an in-flight `read_at` would otherwise
  splice the old file's context onto the new file (identity confusion `new_from_context` can't catch). Each
  slot carries a generation bumped on open/close; the write-back is dropped on mismatch. (Found in review;
  fixed in `9e4c174ed`; host-tested — see below.)
- **Sync→async bridge:** C++ calls `deluge_efatfs_open/_close` synchronously at sample-load; they bridge to the
  async table via `fiber::block_on_fiber`, valid only `on_fiber()`. Off-fiber ⇒ open returns false and the
  stream falls back to the C-FatFS map (never aborts). Off-fiber close can't bridge → the slot leaks until reuse
  (accepted for SP1a; a deferred-close queue is future work).
- **`efatfs_core` (host-testable):** the load-bearing logic (table, generation guard, `FileContext`
  detach/reattach, fill loop) is storage-generic and NOT device-gated, so host tests and future harnesses drive
  the *exact same code*. `efatfs_fs` (device) is a thin wrapper adding the statics/mutexes/FFI/mount.

## Verification state (honest)

**Flag OFF (the merge gate) — solid.** Byte-identical by construction: `deluge_streaming_efatfs_active()` is
false, so `open_read_stream` never opens a handle → `efatfs_handle_` stays 0 → `begin_fill` emits handle 0 →
`ProdOps` uses the unchanged sector path; the Rust boot-mount / `read_at` branch are `#[cfg]`-compiled out.
Default firmware, host sim, and flag-off device builds all pass.

**Host coverage of the real logic — solid.** `fs_differential` (9/9) + `tests/efatfs_core.rs` (3/3) drive the
**real `efatfs_core`** over a host FAT image:
- open → read → whole-file bytes; two-handle round-robin interleave (detach/reattach round-trip).
- **generation-guard recycle test** — a stale commit after a `remove`+`insert` recycle is rejected (the device
  write-back race, untestable on the device statics, covered here).

**Lens-1 margin — measured (proxy), reassuring for sequential playback.** A block-read-counting device shows
embedded-fatfs issues **2109 512-byte reads to sequentially read a 1 MiB / 32-cluster file vs the ideal 2048
(raw-map data-only) = 1.03x overhead** — ~2 FAT-walk reads per 32 KiB cluster, **no O(n) chain re-walk**. So
sequential streaming's SD-transfer cost is ~3% over the raw path. **Backward/loop-point seeks** (the O(n)
re-walk risk) are not covered by this proxy and are exactly what SP1b's cached FAT chain addresses.

**Device builds.** Flag-off links. **Flag-on RELEASE links.** **Flag-on DEBUG does not link** — see below.

## SRAM layout: flag-on debug link overflow — investigated and FIXED

Making the mount live retains the whole `embedded-fatfs` code. At debug `opt-level=1` its un-inlined
monomorphizations (a deep generic storage tower, fully instantiated) inflate `.text` **and** `.ARM.exidx`
(one 8-byte exception-index entry per surviving function — ~19.5 KB of them; in *release* the tower inlines
away and exidx doesn't grow at all). That pushed the debug image ~10 KB past the RTT SRAM reservation.

`.ARM.exidx` cannot simply be discarded — the C++ app *actively uses exceptions* (`throw
deluge::exception::BAD_ALLOC` in the allocators; `catch` in browser/midi/recorder; `-fno-rtti` but **not**
`-fno-exceptions`), so it's required for C++ unwinding.

**Root fix (commit `897a271c6`):** the RTT reservation sat mid-SRAM at `0x202B0000` and capped the app heap
there, stranding ~192 KB of unused SRAM between it and the stacks at `0x202F0000`. Raising `RTT_RAM` /
`NCACHE_RTT_RAM` to `0x202E0000` (just below the stacks) reclaims that gap into the image + `__sram_heap`.
Result: **flag-on debug now links with 128 KB heap headroom** (was a ~10 KB overflow); flag-off debug heap
192 KB → 384 KB. RTT addresses are auto-discovered from the ELF's `_SEGGER_RTT` symbol, so no tooling change
was needed (`_SEGGER_RTT` moves `0x602B4000` → `0x602E4000` on its own). **Needs an on-device RTT-capture
re-verify** (the one owed gate for this change). Release always fit; this makes debug fit too. The 96 KB
`EFATFS_ARENA` is a bench-sized placeholder (BSS, unrelated to the link overflow) and is independently
shrinkable.

## What's deferred / owed

- **On-device flag-on exercise (Kate's gate):** flash the `efatfs_streaming` **release** image, play samples,
  compare `loaded`-miss underruns and audio-routine latency to the flag-off baseline. This is the real proof of
  the live path (host can't exercise the device statics/mutexes/fiber serialization).
- **Fully-integrated Lens-1 margin:** the modeled-latency margin through the *real C++ streaming scenario* in
  `lens1_vt_sim` needs `ProdOps::read`'s efatfs path made host-reachable + a host mount/FFI — a sub-project.
  The transfer-count proxy above is the tractable core signal in the meantime.
- **Flag-on golden:** the golden harness (`tests/golden/run.py`) is absent on this branch (lives on the
  synth-golden-masters line); flag-on streamed-audio byte-exactness is not machine-verified here. Flag-off is
  byte-identical by construction.
- **SP1b:** cache the FAT chain in Rust at open (O(1) random access without the C-FatFS map; the backward/loop
  seek fix), move the sync-fiber read path onto embedded-fatfs, and retire `resolve_read_layout` + the
  `sdAddress` seeding for reads. Only after SP1b are streaming reads fully off C FatFS.

## Foundation dependency (P1) — stated, not hidden

SP1a runs the fill task's embedded-fatfs I/O concurrent with audio. The claim that streaming rungs don't need P1
(preemptive audio, hardware-proven) rests on the fill task being async (it yields on SD) and audio being on a
separate context. The 1.03x sequential proxy is encouraging, but embedded-fatfs's heavier per-read cost makes
the **on-device audio-latency gate the empirical test of that claim**. If latency/margin regress on hardware,
that is the signal P1 must land first. P1 is integrated but not hardware-proven; SP1a does not assume it, but
its on-device gate is where the assumption is validated or falsified.

## Decision

**Software-complete and host-verified; ready for the on-device flag-on exercise on the release image.** The
sequential-read margin is ~3% (proxy). Recommended sequence: on-device flag-on smoke (underrun + latency vs
baseline) → if clean, SP1b (cached chain, which also unlocks cheap loop/backward seeks) → then consider
flipping the flag default. If the on-device latency regresses, that's the P1 trigger.
