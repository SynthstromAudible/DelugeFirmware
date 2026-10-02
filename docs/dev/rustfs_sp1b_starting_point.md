# SP1b — retire the C-FatFS streaming map — STARTING POINT (SUPERSEDED)

> ## ⚠️ SUPERSEDED — READ `docs/superpowers/specs/2026-07-21-sp-stream-read-completion-design.md (local — docs/superpowers is gitignored)` INSTEAD
>
> This document is kept for history. **Three of its load-bearing claims are wrong**, each disproved by
> work on `feat/rustfs-sp1b-cached-chain`:
>
> 1. **The cached FAT chain (its central proposal) was DROPPED.** Its premise — an O(n) chain re-walk on
>    backward seeks — does not survive measurement. A re-walk costs `ceil(k/128)` FAT *sector* reads, not
>    `k` reads, so real looping playback measures **1.03x** and typical samples ~1.6% even under constant
>    seeking. Do not rebuild it. See the superseding doc §1.1 for the measured model and two measurement
>    traps that were hit and caught.
> 2. **"Backward seeks re-walk" understates it** — upstream `seek()` re-walked on *any* cross-cluster
>    seek, including one cluster forward. Fixed in commit `20af6c863` (`VENDOR.md` SP1b-1).
> 3. **"Move the sync-fiber read path" aims at dead code.** `loader::pump` is unreachable whenever
>    `async_streaming_loader` is on, and it is a default feature. The live C-FatFS read path is
>    `read_cluster_data` → `StreamReadSource` via `CLUSTER_LOAD_IMMEDIATELY`, which is unconditional on
>    every build — and it receives `fill.handle` but never consults it.
>
> The work is now framed as completing **SP-stream-read**, whose exit criterion is that nothing above the
> storage port knows what a sector or a cluster is.


**Status:** teed up for a fresh session. **Not an approved spec** — start with `superpowers:brainstorming`
to refine scope/approach, then `writing-plans`. This doc exists so that session doesn't have to re-derive
what SP1a already established.

**Base:** `feat/async-sd-owner-substrate` @ `d276184ba` (SP1a merged). Branch SP1b off that.

## Why this rung matters

SP1a put embedded-fatfs into the live streaming read path but **removed zero C-FatFS consumers** — both
filesystems coexist, with the C-FatFS sector map kept as the flag-off fallback. **SP1b is the rung that
actually retires a consumer:** after it, streaming reads are fully off C FatFS. Without SP1b, SP1a is just a
parallel FS with no retirement progress. (See the SP-stream-read rung in
`docs/superpowers/specs/2026-07-20-async-storage-end-architecture-design.md` §5, and `…-streaming-read-integration-design.md` §4.)

## The three work items (from the SP1a spec §4)

1. **Cache the FAT chain in Rust at open.** Give the per-stream handle a resolved cluster→sector/offset table
   built once at open, restoring O(1) random access without the C-FatFS map. This is "the map, moved into
   Rust". Prefer building it from embedded-fatfs's own FAT reads (may need a small addition to the vendored
   crate).
2. **Move the sync-fiber read path** onto embedded-fatfs, so *both* streaming read paths are off C FatFS.
3. **Retire** `resolve_read_layout`'s FAT walk + `SampleCluster::sdAddress` seeding *for reads*, and delete
   the C-FatFS streaming fallback. (`sd_address_at`/`sdAddress` must STAY for `BlockReadSource` recorder
   read-back until SP-recorder.)

## Why the cached chain is load-bearing (measured, not assumed)

SP1a's Lens-1 margin proxy (`fs_differential/tests/efatfs_core.rs::efatfs_core_sequential_read_transfer_overhead`)
measured **sequential** cluster reads at **1.03x** SD-transfer overhead vs the raw-map ideal (2109 block reads
vs 2048 for a 1 MiB / 32-cluster file) — i.e. ~2 FAT-walk reads per 32 KiB cluster, **no O(n) re-walk**. That
proxy deliberately covers sequential playback ONLY.

**Backward / loop-point seeks are NOT covered and are the open risk:** embedded-fatfs advances
`current_cluster` lazily, so a backward seek re-walks the chain from the start — O(n) per seek. Sample loop
points do exactly that. The cached chain is both the retirement mechanism *and* the fix for this. **Extend the
margin proxy to a backward/loop-seek case early** — that measurement should drive the design, not follow it.

## Code map (verified during SP1a; line numbers approximate, names exact)

**Read paths that must move / retire:**
- Async fill task (already on embedded-fatfs under the flag): `ProdOps::read` in
  `src/bsp/rust/src/streaming_loader.rs` → `efatfs_fs::read_at` when `d.handle != 0`.
- **Sync-fiber path (still C FatFS — SP1b moves this):** `SampleStream::read_cluster_data` →
  `make_read_source()` → `StreamReadSource::read` → `deluge::io::Stream::read_at`
  (`src/deluge/storage/audio/stream/sample_stream.cpp`, `src/fatfs/stream_io.cpp`).
- **The map + FAT walk to retire:** `resolve_read_layout` (`src/fatfs/stream_io.cpp`) seeds
  `table_[i].sdAddress` in `SampleStream::open_read_stream` (`sample_stream.cpp` ~:121);
  read via `SampleStream::sd_address_at` (`sample_stream.h` ~:181), consumed by `begin_fill`
  (`async_fill.cpp` ~:78) as `descriptor.sector`.
- **Keep:** `sd_address_at`/`sdAddress` for `BlockReadSource` (recorder read-back) — not SP1b's to remove.

**What SP1a built that SP1b extends:**
- `src/bsp/rust/src/efatfs_core.rs` — **storage-generic, host-testable** core: `HandleTable`
  (`insert`/`checkout`/`commit`/`remove` + generation guard), `open_context`, `read_context`, `fill`,
  `read_at_owned`. **This is where the cached chain belongs** (generic ⇒ host-testable ⇒ margin-measurable).
- `src/bsp/rust/src/efatfs_fs.rs` — device wrapper: mounted `FileSystem` + `HandleTable` behind two async
  `Mutex`es, `mount()`, FFI `deluge_efatfs_open/_close` via `fiber::block_on_fiber`.
- C-ABI: `StreamingFillDescriptor{handle, byte_offset}` (`include/libdeluge/streaming_fill.h`) +
  `deluge_streaming_efatfs_active()`; weak C++ fallbacks in `async_fill.cpp`.
- Host harness: `fs_differential/tests/efatfs_core.rs` drives the real core (round-trip, interleave,
  generation-guard recycle, margin proxy).

## Constraints carried forward (do not re-litigate)

- **Lock discipline (critical):** `HANDLES` is NEVER held across the FS-mutex await. `open` takes FS→table;
  `read_at` takes table(checkout)→release→FS→table(commit). `HandleTable::read_at_owned` holds the table
  across the await and is **host/sim only** — using it on the device inverts lock order and deadlocks.
- **Generation guard:** slots carry a generation bumped on insert/remove; deferred write-backs are dropped on
  mismatch (prevents a close+reopen recycle splicing a stale context onto a different file). A cached chain
  attached to a slot must respect the same invalidation.
- **No per-read heap alloc** on the hot path. Open may allocate. A cached chain per open file is an
  allocation-at-open — size it deliberately (a 4 GB file at 32 KiB clusters = 131k entries; **bound it**).
- **Coexistence invariant:** stream only stable, already-open files; the recorder writes *other* files via
  C FatFS. Don't stream a file being concurrently written.
- **Flag:** `efatfs_streaming`, default OFF. Flag-off must stay byte-identical. SP1b is where flipping the
  default becomes a real question — gated on the golden + on-device evidence below.

## Gates

- **Golden bit-exact** streamed audio flag-on. ⚠️ **The golden harness is absent from this branch**
  (`tests/golden/run.py` lives on the synth-golden-masters line) — SP1b must either bring it over or state
  the gap. This is the one gate SP1a could not machine-verify.
- **Margin:** re-run the transfer-overhead proxy with the cached chain, and **add the backward/loop-seek
  case**. Cached chain must not regress sequential (1.03x baseline) and must fix loop-seek.
- **Host:** `fs_differential` suite green (run with `SP0_FAT32=/tmp/fat32.img SP0_FAT16=/tmp/fat16.img`;
  rebuild fixtures via `fixtures/mk_fixture.sh /tmp`). Prefer `--test-threads=1` — each test binary loads a
  2.5 GB image and parallel runs can OOM (observed SIGKILL).
- **On-device (Kate's gate):** streaming with the flag on — no underruns, no glitching. SP1a confirmed the
  live mount works on hardware (`efatfs: mounted`) and that efatfs adds **no idle audio-latency overhead**
  (flag-on ≈ flag-off). Note the Rust BSP's *debug* build has a high pre-existing idle audio latency
  (~16–27 ms, unrelated to efatfs) — use the **release** image for any latency-sensitive judgement.

## Open questions to brainstorm

1. **Where does the cached chain live** — in `efatfs_core`'s `HandleTable` slot, or a wrapper type? How does
   it interact with the generation guard on close/recycle?
2. **How is it built** — walk embedded-fatfs's `ClusterIterator` at open, or add a chain-export to the
   vendored crate? Cost at open (a 4 GB sample would walk 131k clusters) vs the O(n)-per-seek it replaces.
3. **Memory bound** — full table, or a sparse/segmented index (every Nth cluster + short walk)? This is the
   real design tension: SP1a's whole point was avoiding per-read alloc; a full chain table is per-open alloc
   that could be large.
4. **Sync-path move** — does `StreamReadSource` call the same `efatfs_core` primitives via a new FFI, or does
   the sync path get retired outright as part of the fiber work?
5. **Sequencing** — cached chain first (measure loop-seek), then sync-path move, then deletion? Or prove the
   deletion is safe first?

## Practical session notes

- Everything above is committed on `feat/async-sd-owner-substrate` @ `d276184ba`.
- SP1a report with full verification state: `docs/dev/rustfs_streaming_sp1a_report.md`.
- The SP1a session exhausted its 200-subagent cap; a fresh session restores subagent-driven execution.
- SRAM layout note: the RTT reserve was raised to `0x202E0000` (commit `897a271c6`), reclaiming ~192 KB into
  the image+heap — this is why the flag-on **debug** image now links. `_SEGGER_RTT` is auto-discovered from
  the ELF, so no tooling addresses are hard-coded. Hardware-verified.
- Optional independent cleanup, never done: shrink `EFATFS_ARENA` (96 KiB bench-sized placeholder, BSS).
