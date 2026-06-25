# SDRAM allocator strangle + stealable taxonomy cleanup

The plan for moving the SDRAM half of `GeneralMemoryAllocator` off the C++ `MemoryRegion` onto the Rust
`deluge_alloc` core, *and* cleaning up the "stealable" set so the pager has a coherent shape. Read
[`allocator_redesign.md`](./allocator_redesign.md) (the *what/why*),
[`allocator_implementation_plan.md`](./allocator_implementation_plan.md) (phases), and
[`rt_allocation_audit.md`](./rt_allocation_audit.md) first.

## Context

Phase 3 is done: a Rust `no_std` TLSF + slab core (`crates/deluge_alloc`) behind the `libdeluge/alloc.h`
C ABI, tested host-side (unit/proptest/fuzz at 32- and 64-bit) and against the C++ conformance suite, and
cross-compiling for `armv7a`. The firmware's **internal/SRAM** region already routes through it
(`GeneralMemoryAllocator`, gated `#if DELUGE_USE_RUST_ALLOC`, firmware-only).

This is the **SDRAM** half — the hard part, because SDRAM holds the "stealables" (the sample-streaming
pager). Investigation (3 explore passes) showed the stealable set is **heterogeneous partly by accident**,
which is what made the slab look like a poor fit. Cleaning that up first makes the slab end-state clean.

**No hardware/goldens on this branch.** The verification path is the **host-sim** (it builds the same
`deluge_app` and runs the stem-renderer end-to-end) — see Verification.

## The stealable taxonomy (decided)

| Object | Size | Today | Target | Rationale |
|---|---|---|---|---|
| `Cluster` | ~32 KB **uniform**, hundreds–thousands | stealable, `MemoryRegion` STEALABLE | **the cache** — slab-backed, priority eviction | large, numerous, reloadable from SD; the legit pager traffic |
| `WaveTableBandData` | **variable** | stealable, in cluster queue | **registered reclaimable** (general heap) | reloadable cache but not uniform → not slab traffic |
| `GrainBuffer` | ~512 KB fixed (`StereoSample[65536]`), few | stealable, parked high-priority in cluster queue | **registered reclaimable + lazy-allocate / idle-release** | scratch (not SD-reloadable); shoehorned into the cluster queue today |
| `Sample`/`WaveTable` (`AudioFile` headers) | small metadata | stealable (`NO_SONG_AUDIO_FILE_OBJECTS`) | **registered reclaimable** | genuinely a metadata cache that yields (fast re-load) — keep reclaimable, just off the cluster queue; the `// bad idea` was about *where* (cluster region), not *whether* |
| `GranularProcessor` | fixed FX state (not even a `Stealable`) | `allocStealable` to land in SDRAM | **demote** → plain SDRAM alloc | not a `Stealable`, never actually stolen today — pure mis-placement; the only true demotion |

Note on `AudioFile`: it's reclaimable, but small. Demoting it to non-reclaimable would lose the loaded-file
metadata cache and require `audio_file_manager` lifecycle rework (delete-on-unreferenced) — so it moves to
the registered-reclaimable mechanism, **not** to a plain alloc.

Net: after cleanup the **cluster priority queue contains only clusters** (uniform, reloadable) → the slab's
sole, natural tenant; everything else is either a plain alloc (`GranularProcessor`) or a registered
reclaimable (`AudioFile`, `WaveTableBandData`, `GrainBuffer`).

## Target design

- **One Rust SDRAM heap** `externalHeap_` over `[&__sdram_bss_end, deluge_memory_external_end())` — the
  three contiguous carves (STEALABLE + EXTERNAL 2 MiB + EXTERNAL_SMALL 128 KiB) collapse into one heap
  (TLSF handles small+large; no static sub-partitions → *more* unified than today).
- **Reclaim hook coordinator** registered on `externalHeap_` (`deluge_heap_register_reclaim`). On
  out-of-space it: (1) evicts the coldest unpinned **cluster** by the existing 8 cluster priority levels;
  (2) failing that, asks the **registered reclaimables** (wavetable bands, grain buffers) to yield. Returns
  true if it freed anything; `deluge_alloc`'s depth-1 retry loop re-drives it.
- **`thingNotToStealFrom`** is threaded via a `GeneralMemoryAllocator` member set around the allocating
  call (the Rust `reclaim(ctx, bytes)` hook has no such parameter; single-threaded so a member is safe).
- **Clusters**: keep the C++ priority-queue eviction *policy* (`CacheManager`, minus the deleted
  neighbour-grab); back them with `deluge_alloc` first (uniform size ⇒ TLSF size-class reuse already packs
  like a slab), then move to `deluge_slab` with priority buckets as the clean end-state (step 5).
- **Registered reclaimables**: a small mechanism — a list of `{may_reclaim() -> bool, reclaim()}` owners
  the coordinator consults. `WaveTableBandData` and `GrainBuffer` register here instead of the cluster
  queue (the redesign doc's "reclaimable owners that don't go through the cluster slab").
- **GrainBuffer lazy/idle-release**: allocate on first grain render, free on idle-timeout / stop (so a
  *configured-but-inactive* granular FX holds 0 bytes, not 512 KB); the reclaim callback covers the
  under-pressure `!inUse` case. Mitigate re-activation thrash with the idle timeout.

## Keep vs delete in the C++ pager

From the reclaim/steal audit:
- **Keep (policy):** the 10→(8 cluster) priority queues, `mayBeStolen(thingNotToStealFrom)`,
  `getAppropriateQueue` lazy re-sort, the final `steal()` + `~Stealable()`, and the
  `numRefusedTheft>=512`/`bypassCulling` voice-cull interaction.
- **Delete (MemoryRegion-specific):** `attemptToGrabNeighbouringMemory`, `writeTempHeadersBeforeASteal`,
  the `stealable-4` header-word size read, `longest_runs_` (the neighbour-run cache), and the
  raw-address return of `CacheManager::reclaim` (the Rust hook returns `bool`).

## Sequencing (each step builds + keeps host tests green; behavioral steps verified in the sim)

0. **Introduce the registered-reclaimable mechanism** (the substrate steps 1-3 register with): a small
   list of `{may_reclaim() -> bool, reclaim()}` owners the SDRAM reclaim coordinator consults after the
   cluster cache. Lives next to the heap/coordinator (a thin C++ registry now; could move into the Rust
   core later).
1. **GranularProcessor demotion** (trivial/safe): `allocStealable` → `allocExternal` at
   `mod_controllable_audio.cpp:1746`. It's not a `Stealable` and is never stolen today, so this only moves
   it out of the stealable region — no eviction behavior changes.
2. **GrainBuffer** → registered reclaimable + lazy-allocate/idle-release. Files:
   `dsp/granular/GranularProcessor.{h,cpp}`.
3. **WaveTableBandData** → registered reclaimable; `shortenRight`→`realloc` (in-place shrink),
   `shortenLeft`→`memmove`+`realloc` (or accept the small left-trim waste initially). Files:
   `storage/wave_table/wave_table.cpp`, `wave_table_band_data.{h,cpp}`.
3b. **AudioFile headers** (`Sample`/`WaveTable`) → registered reclaimable (off the cluster queue), keeping
   the reason-count-driven reclaimability. Files: `audio_file.{h,cpp}`.
4. **SDRAM heap + reclaim coordinator.** Collapse STEALABLE/EXTERNAL/EXTERNAL_SMALL into `externalHeap_`;
   route `allocExternal` + cluster `allocStealable` → `deluge_alloc`; rework `CacheManager::reclaim` into
   the bool reclaim hook (cluster-priority eviction, no neighbour-grab); extend `rustHeapFor` to the SDRAM
   range. All gated `#if DELUGE_USE_RUST_ALLOC`. Files: `general_memory_allocator.{h,cpp}`,
   `cache_manager.{h,cpp}`, `cluster.cpp`.
5. **Slab-ize clusters** (the Option-A end-state): `Cluster::create` → `deluge_slab_acquire` with priority
   buckets mirroring `getAppropriateQueue`; coordinator frees cluster victims via `deluge_slab_release`.
   Requires extending `deluge_slab` with priority buckets.
6. **Delete** `MemoryRegion::attemptToGrabNeighbouringMemory`/`writeTempHeadersBeforeASteal` and the old
   `CacheManager` neighbour-grab; eventually retire `MemoryRegion` once the sim/tests also flip.

## Verification

- **Build:** `./dbt build debug` → `deluge.elf` links with the Rust symbols pulled in; host unit tests
  (`MemoryManagerTests`, `memory_specs`, `alloc_specs`) stay green (gate off).
- **Behavioral (the key one):** wire the **host-sim** to a host `deluge_alloc` staticlib (`i686` to match
  the sim's `-m32`, or `x86_64` for `DELUGE_SIM_X64`) and define `DELUGE_USE_RUST_ALLOC` for the sim
  (`sim/CMakeLists.txt`). Then run the stem-renderer (`deluge_render`, known working end-to-end) on the
  Rust allocator and confirm it produces valid audio; diff against the C++-allocator render. This is the
  only behavioral check available without hardware — do it early (after step 4) so the SDRAM rewire is
  exercised, not just compiled.
- **Rust side:** the slab priority-bucket extension (step 5) gets the same unit/proptest/fuzz treatment as
  the existing slab.

## Risks / open questions

- **Unverifiable on hardware here** — leans entirely on the sim + Rust tests. Flag firmware artifacts
  NEEDS-HARDWARE before any merge.
- **Demotions remove last-resort metadata eviction** — a pathological tight-RAM song could OOM where it
  currently limps. Small (metadata is small) but real; watch in the sim.
- **GrainBuffer lazy/idle** changes the granular FX memory lifecycle (re-alloc + re-clear latency on
  re-activation; thrash if toggled — mitigated by idle timeout).
- **Reclaim reentrancy across FFI** — already handled by `deluge_alloc`'s depth-1 guard + the retry loop
  living in the C ABI (no `&mut Tlsf` across the callback).
- **Unified-pool yielding** — confirm a large general alloc actually reclaims cold clusters (the whole
  point); TLSF coalescing of freed (non-adjacent) cluster blocks may need several reclaim iterations for a
  large contiguous request — acceptable, but verify under the memory-pressure sim songs.

## Execution status (2026-06-21)

Steps 1–4 landed earlier (SRAM+SDRAM on the Rust heap behind `DELUGE_USE_RUST_ALLOC`; typed allocators off
`GMA::get()`; `CacheManager::reclaimOne` drives the SDRAM reclaim hook `gmaSdramReclaim`). Then:

- **Verification harness (plan "Step 5").** Fixed `host_render_main.cpp` (`.get()`→`.c_str()`); `deluge_render`
  now builds + runs the `DELUGE_RENDER_MAKE_TEMPLATE` smoke gate-on **and** gate-off (both exit 0). The
  bit-exact A/B + cluster-eviction stress still needs a memory-pressure project (user-supplied) — pending.
- **GrainBuffer lazy + idle-release (plan "Step 6"), DONE.** Dropped the eager `getBuffer()` in both
  `GranularProcessor` ctors (allocated on first render instead); `startSkippingRendering()` and the
  `wrapsToShutdown < 0` idle branch now `releaseBuffer()` (free + null + reset) rather than only
  `inUse=false`. Also reordered `processGrainFX` so `getBuffer()` runs **before** `setWrapsToShutdown()` —
  the latter dereferences `grainBuffer`, which was a latent null-deref once the buffer could be released or
  stolen and sound then resumed. `~GranularProcessor` reuses `releaseBuffer()`.
- **Slab-ize clusters (plan "Step 7"), DONE — *backing-only*, not priority-bucketed.** The original sketch
  put priority buckets in the slab; instead eviction policy stays in `CacheManager` (clusters remain on the
  `StealableQueue`s) and the slab is pure uniform backing. New Rust API:
  `deluge_slab_create_unmanaged` (registers no reclaim hook, never self-evicts; alloc-first acquire so the
  heap hook can recycle table entries reentrantly) and `deluge_slab_release` now returns `bool` (owned?) for
  "slab-release else heap-free". C++: `GMA::acquireCluster` lazily creates `clusterSlab_` over `sdram_heap()`
  on the first `Cluster::create`, with `slot = sizeof(Cluster)+Cluster::size` — safe because the firmware
  **never raises** cluster size after boot (`AudioFileManager::cardReinserted` rejects bigger cards), so the
  first-create size is the session maximum; uniform slots fit every cluster. `GMA::freeSdram` does
  slab-release-else-`deluge_free`; `Cluster::create/destroy` and `gmaSdramReclaim` all route through it.
  Capacity = `sdram_size()/slot + 1` so the table is never the limiter.
  - *Follow-up:* `deluge_slab_release`/`find_by_ptr` is an O(capacity) scan per cluster free; fine for the
    correctness milestone, optimize later (back-pointer/index in a header) if profiling shows it.

**Verified:** `deluge.elf` Debug+Release link; sim gate-on boots + render-smoke (exit 0); sim gate-off builds;
`cargo test` 16/16 on host + i686; ctest 10/10 (incl. `memory_specs`, `alloc_specs`); cargo fmt + clang-format
clean. **Still NEEDS-HARDWARE**, and the behavioral cluster-eviction A/B awaits the memory-pressure project.

Plan "Step 8" (retire `MemoryRegion`, remove the gate, strangle remaining `GMA::get()` callers, handle
`INTERNAL_SMALL`/frunk) is intentionally **deferred** until that behavioral verification passes.

## Step 8 — MemoryRegion retired (2026-06-21) — DONE, gate removed, NEEDS-HARDWARE

The legacy C++ allocator is gone; the Rust deluge_alloc core is now **unconditional** (no
`DELUGE_USE_RUST_ALLOC` gate). User chose the full retirement now, accepting loss of the gate-off C++
A/B baseline (verification leans on the Rust tests + on-hardware ear-test).

- **Deleted:** `memory/memory_region.{h,cpp}`, `memory/reclaimer.h` (the Phase-1 inversion seam — its only
  point was decoupling MemoryRegion), `memory/empty_space_vector.h` + its spec (only MemoryRegion used it).
- **`CacheManager` trimmed to policy-only:** dropped `reclaim()` (the neighbour-grab/run-length/header-read
  path), `longest_runs_`, the `Reclaimer` base, and the `currentTraversalNo`/`skipConsistencyCheck` globals.
  Kept the priority queues + `QueueForReclamation` + `reclaimOne` (the victim selection the heap reclaim
  hook drives). It no longer inherits anything.
- **`GeneralMemoryAllocator` reduced to the coordinator facade:** no `regions[]` / `getRegion` /
  `rustHeapFor` / `MEMORY_REGION_*` / `RESERVED_*` / emptySpaces buffers. The ctor is now just
  `init_heaps()` + register the SDRAM reclaim hook. alloc/dealloc/resize forward to the deluge::memory
  heaps; `allocStealable` keeps the `currentDontStealFrom_` coordination; cluster alloc stays on the slab.
- **Frunk (small-internal SRAM) moved to a heap:** `deluge::memory::frunk_heap()` built from the
  `__frunk_bss_end`/`__frunk_slack_end` linker symbols in `heaps.cpp` (the app layer may reference linker
  symbols; the Rust crate stays symbol-free). `allocInternal` routes <128 B there, else the SRAM heap.
  Added `owning_heap()` (address→heap, replaces `rustHeapFor`/`getRegion`), `sram_size()`, `sdram_size()`.
- **App couplings rehomed:** `sound_editor.cpp` info display → `sram_size()`; `sample_cache.cpp`
  cluster-in-SDRAM asserts → `owning_heap(x) != sdram_heap()`. `operators.cpp` global new/delete →
  `deluge::memory::alloc_external`/`dealloc`.
- **Intentionally NOT repointed:** `memory_allocator_interface.cpp` (allocMaxSpeed/LowSpeed/Stealable —
  rely on the GMA coordinator's deterministic-zeroing + frunk + internal-first + steal semantics) and
  `live_pitch_shifter.cpp` (`allocMaxSpeed`). GMA is now a legitimate thin coordinator facade; callers
  using it is fine. Bare heap helpers don't replicate those semantics, so repointing would change behavior.
- **CMake:** the Rust staticlib build/link is now unconditional for firmware (cross) + sim; removed the
  `DELUGE_USE_RUST_ALLOC` define and the `SIM_RUST_ALLOC` option. Retired the `MemoryManagerTests` and
  `memory_specs` test targets; kept `alloc_specs` (Rust conformance) + the shared conformance headers in
  `tests/spec_memory/` + the reusable mocks in `tests/32bit_unit_tests/mocks/` (pulled in by `spec/`).

**Verified:** deluge.elf Debug+Release link; sim boots + render-smoke (exit 0); `ctest` 7/7 incl.
`deluge_alloc_conformance_spec`; no dangling refs to the removed constructs; clang-format clean.

**Follow-ups (optional):** the `deluge_slab_release`/`find_by_ptr` O(capacity) scan; folding frunk into a
third BSP `deluge_memory_region` (DELUGE_MEM_SMALL_INTERNAL) so heaps.cpp drops the linker-symbol coupling;
the eviction-policy → resource/file-manager refactor (the home for any future priority indexing).

## Step 8b — GMA "cheap track": shrunk to coordinator-only (2026-06-21) — DONE, NEEDS-HARDWARE

Migrated the pure-allocation public surface off `GeneralMemoryAllocator::get()` so the GMA is now *only*
the stealable/reclaim coordinator. (The other half — relocating the coordinator itself — is deferred to the
resource/file-manager refactor, since `CacheManager` + cluster slab belong there, not in `deluge::memory`.)

- **`deluge::memory` is now the allocation surface.** `alloc_fast`/`alloc_sdram`/`alloc_external` gained
  default `align=16`, the `DELUGE_DETERMINISTIC_ALLOC` zeroing (moved from the GMA's `finalizeAlloc`; now
  also covers the typed allocators — strictly more determinism, sim-only), and `alloc_fast` routes
  `<128 B` to the frunk heap then SRAM then SDRAM (== the old `allocMaxSpeed`). Added `shrink()` (==
  `shortenRight`) and `shrink_left()` (no-op→0, == `shortenLeft` on the Rust heap).
- **Migrated ~115 call sites:** `allocMaxSpeed`→`alloc_fast` (60), `allocLowSpeed`→`alloc_sdram` (43),
  `dealloc`→`deluge::memory::dealloc` (6), `shortenRight`→`shrink`/`shortenLeft`→`shrink_left` (3).
- **`checkStack` evicted** to standalone `memory/stack_guard.{h,cpp}` (it's a stack-overflow guard, not an
  allocator concern); 6 callers updated + given the include.
- **Trimmed the now-dead GMA methods:** `allocMaxSpeed`/`allocLowSpeed` (the inline wrappers),
  `deallocExternal`, `getAllocatedSize`, `shortenRight`/`shortenLeft`, `extend`/
  `extendRightAsMuchAsEasilyPossible`, `checkStack` — all had zero callers after the migration.

**GMA is now coordinator-only:** `allocStealable` (the one alloc that threads `thingNotToStealFrom` →
`currentDontStealFrom_`), `alloc`/`allocInternal`/`allocExternal` (its internals), `dealloc`,
`acquireCluster`/`freeSdram` (cluster slab), `putStealable*`/`getCacheManager` + `cacheManager`, the
`gmaSdramReclaim` hook, and `test()` (gated). general_memory_allocator.cpp 476 → 212 lines.

**Remaining `GMA::get()` callers (all coordinator, expected):** `allocStealable` ×5 (GrainBuffer,
sample_recorder, audio_file_manager, mod_controllable_audio, wave_table), the `alloc` interface-wrappers
in `memory_allocator_interface.cpp`, `putStealable*`/`getCacheManager` (cache owners), `acquireCluster`/
`freeSdram` (cluster.cpp). These dissolve when the coordinator moves to the resource/file-manager refactor
(`deluge_heap_register_reclaim(hook, ctx)` is the only allocator-side seam) — at which point the GMA class
disappears entirely.

**Verified:** firmware Debug (`dbt build Debug`) links; sim boots + render-smoke; `ctest` 7/7; clang-format
clean.
