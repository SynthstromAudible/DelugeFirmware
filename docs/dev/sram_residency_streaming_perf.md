# SRAM-residency tiering for sample streaming performance

**Status:** investigation + design proposal, nothing implemented. NEEDS-HARDWARE to verify (see
Verification). Hand-off doc for a session resident in this repo.

**Target branch:** the audio-stream refactor lives on **`next`** (`src/deluge/storage/audio/stream/`,
`crates/deluge_resource`, `crates/deluge_alloc`). Base new work on `next` (or a branch off it). The
reference docs below (`allocator_sdram_strangle.md`, `allocator_redesign.md`) were *removed from `next`*
but still exist on `deepclone`, `deluge-project`, `feat/immediate-mode`, `feat/synth-golden-masters` —
`git show deepclone:docs/dev/allocator_sdram_strangle.md`.

---

## TL;DR

When sample data moved from on-chip SRAM to the external SDRAM IC, two things got worse and one is at
risk:

- **(A) Simultaneous large streams** degrade super-linearly (dropouts under polyphony).
- **(B) Small drum samples** — fully RAM-resident, never actually streaming — got *slow to render*.
- **(C) Attack transients / dense onsets** (downbeats, chord stabs) concentrate cold SDRAM reads at the
  worst moment.

Root cause is **hardware**, not the allocator: the external memory is a *single 16-bit-wide SDRAM device
behind one bus controller*, and concurrent scattered read cursors are close to its worst-case access
pattern. None of this is visible in the host-sim.

The proposal is a **single SRAM-residency budget with priority tiers**, keyed on sample size and voice
activity, wired into the backing-selection seam the audio-stream refactor already created. Whole small
samples and the *heads of active voices* live in SRAM; large streamed tails stay in SDRAM.

---

## 1. Hardware root cause (the discovery)

Numbers are the **register configuration the firmware programs**, in deluge-sdk's
`crates/deluge-bsp/src/sdram.rs` (`init`, called from `system.rs` at bring-up) — a port of the retired C HAL's
`src/RZA1/bsc/bsc_userdef.c` (`userdef_bsc_cs2_init`) with the same values. Ground truth for the part, since the
SDRAM works so the config must match it:

- **External SDRAM — confirmed part: Alliance Memory `AS4C32M16SM-7TCN`** (from Synthstrom, not the repo).
  512 Mbit = **64 MB**, **32M × 16** (16-bit bus), **4 banks × 8M × 16**, **13 row / 10 column** bits, `-7`
  = 7 ns / 143 MHz-capable, TSOP-II, 3.3 V. Run here at **CAS latency 2** off the RZ/A1 external bus clock,
  auto-precharge, **burst length 1**, auto-refresh ~7.64 µs / 128 cycles. One device, one BSC channel
  (SDRAM on CS2/CS3).
  - Every figure above is corroborated by the BSC register config the firmware programs
    (deluge-sdk `crates/deluge-bsp/src/sdram.rs`): 16-bit (`CS2BCR/CS3BCR = 0x00004C00`), 64 MB /
    13-row-10-col (`SDCR = 0x00110912`), CAS 2, burst 1 (`SDRAM_MODE = 0`), refresh 128 cyc (`RTCOR = 0x80`).
    The register geometry matches the datasheet exactly — the "4 banks" this analysis rests on is confirmed.
  - The part is a **JEDEC-standard 512 Mbit ×16 SDRAM**, pin-compatible (54-TSOP-II) with the drop-in
    equivalents named in the C HAL's *inherited RSK+ sample-code comments* (and in `sdram.rs`'s) — Micron `MT48LC16M16A2P-75` (a
    32 MB part; wrong size) and ISSI `IS42S16320B-75` (64 MB). Those comments do **not** identify the fitted
    silicon; the Alliance part above is the real one. The same BSC config drives any of these interchangeably.
  - Synthstrom **already tuned** BSC timing: a comment in the C HAL's `bsc_userdef.c` noted "WTRCD made quite a big difference /
    AC3L can't be reduced" — i.e. the CAS-related floor is already hit (see §5b.4).
- **Memory map** (the C HAL's `src/RZA1/cpu_specific.h`; the Rust HAL's `memmap.rs` agrees): `EXTERNAL_MEMORY_BEGIN 0x0C000000`, `EXTERNAL_MEMORY_END
  0x10000000` (64 MB SDRAM); `INTERNAL_MEMORY_BEGIN 0x20000000` (~3 MB on-chip SRAM, multi-ported,
  refresh-free); `UNCACHED_MIRROR_OFFSET 0x40000000` (an uncached alias exists but is unused by the
  streaming path).
- **Cache mapping**: SDRAM (area 1) is mapped normal-cacheable — write-back, write-allocate — as the C HAL's
  `ttb_init.S` (`setting_area1`, `TTB_PARA_NORMAL_CACHE`) had it and the Rust HAL's `mmu.rs` (`PARA_NORMAL_CACHE`,
  with the 0x4C00_0000 mirror uncached) has it now.

### Why simultaneous streams collapse (4 stacking mechanisms)

1. **Bus serialization.** Every cluster read (render), every SD→SDRAM DMA fill (loader), and the OLED
   framebuffer funnel through one 16-bit external channel. Idle at 1 stream; contended at N.
2. **Row/bank thrashing — the dominant one.** SDRAM is fast only *within an already-open row*. N
   independent streams read scattered regions → constant precharge→activate→CAS as rows switch.
   Effective bandwidth falls far below sequential peak. **This is why degradation is super-linear.**
   Internal SRAM has no row concept — random == sequential cost, so it never had this penalty.
3. **Cache can't hide streaming.** Read-once data with no temporal reuse; N streams' working set dwarfs
   L1/L2 → ~0 hit rate → full miss latency. Write-allocate on cache-generating paths doubles bus traffic
   (read-fill + writeback).
4. **Refresh + DMA contention.** Auto-refresh steals cycles; render reads collide with loader writes on
   the same device with bank conflicts.

Net: internal SRAM was low-latency, wide, multi-ported, refresh-free, locality-insensitive. SDRAM is the
opposite on every axis, and streaming's many-scattered-cursors pattern is its worst case.

---

## 2. Current repo state (what the audio-stream refactor already built)

This is the crucial context: the **structural groundwork is done**, so the proposal is mostly a
placement-policy change, not new plumbing.

### The chunk-type split (the enabling refactor)

`Cluster` was split into two distinct types (see `storage/cluster/cluster.h`):

- **`StreamedChunk`** — file-backed, reloadable-from-SD sample audio (the streaming pager traffic).
- **`ComputedChunk`** — perc-cache + repitch/sample-cache scratch (expensive to recompute).

Before this they were one overloaded `Cluster` ("largely legacy"). The split is what makes per-class
placement possible at all.

### The Rust allocator + resource manager

- `crates/deluge_alloc` — `no_std` TLSF heap + **slab pager** (`src/slab.rs`). Slab = fixed-capacity table
  of uniform slots borrowed from the TLSF heap; registers as the heap's reclaim hook and evicts the
  coldest **unpinned** slot on pressure (`EvictFn`, the `Stealable::steal()` analogue). Pinning ==
  `numReasonsToBeLoaded`.
- `crates/deluge_resource` — resource manager (`src/manager.rs`): assets + individually-leased/evicted
  chunks, priority-queue eviction (the C++ `CacheManager` policy drives the reclaim hook), a **backing
  selector** per asset: `DELUGE_RESOURCE_BACKING_HEAP` (variable, TLSF) vs `DELUGE_RESOURCE_BACKING_SLAB`
  (uniform clusters). C ABI in `crates/deluge_resource/include/deluge_resource.h`.
- `docs/dev/allocator_sdram_strangle.md` (on the branches listed above): collapsed
  STEALABLE/EXTERNAL/EXTERNAL_SMALL into one Rust SDRAM heap, slab-backed clusters, retired
  `MemoryRegion`, moved GrainBuffer/WaveTableBandData/AudioFile off the cluster queue to registered
  reclaimables.

### Memory API (`src/deluge/memory/heaps.h`)

```
DelugeHeap* sram_heap();     DelugeHeap* sdram_heap();     DelugeHeap* frunk_heap();
DelugeHeap* owning_heap(void* ptr);     std::size_t sram_size();   std::size_t sdram_size();
void* alloc_fast(size,align);   // SRAM-preferred (small→frunk), SDRAM fallback
void* alloc_sdram(size,align);  void* alloc_external(size,align);
```

### The key limitation for this work

Both chunk types currently land in **one SDRAM slab**:

- `crates/.../general_memory_allocator.cpp:80` —
  `clusterSlab_ = deluge_slab_create_unmanaged(deluge::memory::sdram_heap(), slot, cap)`.
- `general_memory_allocator.cpp:99-101` — `resourceManager_ = deluge_resource_create(sdram_heap(), …)`
  then `deluge_resource_set_slab(resourceManager_, clusterSlab_)`. **One manager, one heap, one slab.**
- `storage/audio/stream/sample_stream.cpp:96` — StreamedChunk asset defined with `..._BACKING_SLAB`.
- `model/sample/sample_cache.cpp:76` — ComputedChunk asset defined with `..._BACKING_SLAB`.

So the *types* are separated but *placement is uniform SDRAM*. The manager knows only `sdram_heap()` and a
single slab. **SRAM residency for clusters requires teaching the manager about a second pool (an SRAM
slab and/or SRAM heap) and selecting it per-asset** — see §5.

---

## 3. The three problems, precisely

### (A) Large simultaneous streams — dropouts

Inherent to §1. The render read path hands the DSP a raw pointer into SDRAM and the inner loop
dereferences it per output sample:
- `model/sample/sample_low_level_reader.cpp:44,69` — `currentPlayPos = …clusters[0]->payload().data()…`.
Prefetch depth is only `kNumClustersLoadedAhead = 2` (`src/definitions_cxx.hpp:659`) — current + next
cluster — the entire margin against SDRAM latency variance.

### (B) Small drum samples render slow — a *different* problem

A sample ≤ `kNumClustersLoadedAhead` clusters is **already fully resident** (heads pinned, see (C)); it
never streams. Its slowness is **render-inner-loop latency**: the per-sample pointer deref now hits SDRAM
instead of SRAM. A drum-heavy kit renders many voices per frame, each fetching a *different* scattered
SDRAM sample → row switches + working-set cache overflow → render time balloons. On the old all-SRAM box
this was free. **Prefetch depth is irrelevant here (nothing to prefetch); uncached mapping would make it
worse (a re-triggered drum has real reuse).**

### (C) Attack transients / dense onsets — head-cluster placement

The Deluge permanently pins the first `kNumClustersLoadedAhead` clusters of a sample so playback starts
instantly (`storage/audio/audio_file_manager.h` doc). These are held **per `SampleHolder`** —
`model/sample/sample_holder.cpp` (`clustersForStart[]`, populated in `setAudioFile`/`loadClusters`
~L190-230). **Critical fact:** they're held for *every sample referenced anywhere in the song*, whenever
assigned to a holder, **even when idle** — not just for playing voices.

Head placement does **not** change SD-read pressure (heads are resident either way; SD reads are for the
streamed tail). What it changes:
1. Attack-transient render latency — the most audible moment — is SDRAM-latency-bound when heads are in
   SDRAM, and that cost concentrates when many voices trigger on one tick.
2. At that same instant the loader is DMA-*writing* tail clusters into SDRAM. The onset makes SDRAM serve
   head-reads **and** tail-writes on the one 16-bit bus simultaneously. Moving heads to SRAM **takes the
   onset render reads off the SDRAM bus, freeing bandwidth for the concurrent tail-fill** — the pressure
   that actually causes dropouts. (This is the strongest form of the argument.)

Because heads are held for *all referenced* samples (not just playing ones), pinning **all** heads in
SRAM is **not affordable** — footprint scales with the song's total sample count (a full kit + multisample
synths = dozens–hundreds × 2×32 KB), which would overflow the ~3 MB SRAM. The knob must be **activity**,
not "always."

---

## 4. Proposed design: one SRAM-residency budget, priority tiers

A single reserved SRAM cluster budget, filled by priority, with **SDRAM fallback** (never refuse a
sample; existing reclaim handles pressure):

1. **Tier 1 — whole small samples.** Sample total ≤ threshold *N* clusters → back its `StreamedChunk`s
   from SRAM. Fixes (B). Biggest bang/buck; these are the hot, re-triggered oneshots (drums) and they fit.
2. **Tier 2 — heads of *active* voices.** Promote a large sample's head clusters to SRAM **when its voice
   starts** (or is imminently sequenced), demote back to SDRAM when idle. Bounds cost to ~polyphony, not
   the song's sample count. Fixes (C). Net-new benefit beyond Tier 1 = large *multisampled* voices played
   polyphonically (chords/stabs), where each voice's head is a different scattered SDRAM region.
3. **Tier 3 — streamed tails.** Stay in SDRAM (can't fit, and they're latency-tolerant via prefetch).

Threshold/budget are the tuning knobs: start with a fixed per-file cluster cap (*N*) **and** a global SRAM
cluster budget cap; admit Tier 1 then Tier 2 until the budget is exhausted, then fall back to SDRAM.

### Diminishing returns to keep in mind
- Long, rarely-triggered sustained samples: heads-in-SRAM only helps the first ~2 clusters; steady state
  is SDRAM regardless. Don't over-invest there.
- Pre-warming (promote a tick early) only works for *sequenced* playback; live note-on eats one cold read
  on the first note.

---

## 5. Implementation seam & the open architectural choice

The per-sample decision keys in at **`sample_stream.cpp:96`** (and, for Tier 1 of ComputedChunks if
desired, `sample_cache.cpp:76`) — currently hardcoded `DELUGE_RESOURCE_BACKING_SLAB`. Two ways to give the
manager an SRAM pool:

- **Option 1 — second slab (uniform, matches today's model).** Add an SRAM cluster slab
  `deluge_slab_create_unmanaged(sram_heap(), slot, sramCap)` alongside `clusterSlab_`
  (`general_memory_allocator.cpp:80`). Requires the resource manager to support *two* slabs and a per-asset
  slab selector (today `deluge_resource_set_slab` sets exactly one — `deluge_resource.h:117`). Rust change
  in `crates/deluge_resource` + a new backing constant or a slab-id on the asset.
- **Option 2 — SampleStream owns a small SRAM pool directly** for Tier-1/Tier-2 chunks, bypassing the
  resource manager for those, allocating from `sram_heap()` / a dedicated SRAM slab. Simpler manager, but
  you lose unified priority eviction for the SRAM tier and must hand-roll demotion. Trade-off.

Recommend **Option 1** (keeps one eviction policy) unless the Rust two-slab change proves heavy, in which
case Option 2 is a faster spike.

`owning_heap(ptr)` (`heaps.h:52`) lets asserts/telemetry confirm which tier a chunk actually landed in.

### Interaction with the other two candidate fixes (from the same investigation)
- **Deeper prefetch** (raise/parameterize `kNumClustersLoadedAhead`): helps (A) only. Orthogonal, cleaner
  now via `SampleStream` + `loader::pump`. Make it per-source if raising globally costs too much RAM.
- **Uncached mapping for streamed tails** (route Tier-3 payloads through `UNCACHED_MIRROR_OFFSET`): read-
  once tails gain nothing from caching and cost writeback/pollution bus traffic. Safe **only once residency
  class is explicit** — must NOT apply to Tier-1/Tier-2 (reused → want cached/SRAM). The tiering is what
  makes uncaching tails coherent instead of harmful.

---

## 5b. Complementary axis — optimizing the SDRAM access itself (coalescing / bank-awareness)

Tiering (§4) *moves hot data off* SDRAM. This is the **orthogonal** axis: make the traffic that *stays* on
SDRAM — the streamed tails, which are the real bandwidth consumers — cheaper. It's the **direct** attack on
problem (A)'s dominant mechanism (row/bank thrashing, §1.2), which tiering only sidesteps by removing some
traffic. Do these *after* tiering (lower-risk, ear-verifiable) — but they compose with it.

**Current read granularity:** `SampleStream::read_cluster_data` (`sample_stream.cpp:150`) reads exactly
**one FAT cluster per call** (`numSectors = Cluster::size >> 9`), one stream advancing one cluster per
`loader::pump` iteration. One FAT cluster *is* physically contiguous on the card, so the read is already
coalesced to cluster granularity; the thrash is on the **SDRAM device**, from (i) interleaving DMA-writes
(tail fills) with render-reads across many voices, and (ii) concurrently-active clusters landing in
arbitrary slab slots → arbitrary SDRAM rows/banks.

Levers, ranked by leverage × tractability:

1. **Bank-aware placement — highest leverage.** The part has **4 internal banks** (geometry-derived, §1 —
   holds regardless of the exact part number). The cluster
   slab hands out slots by availability, so two concurrently-streaming clusters likely share a bank → their
   interleaved accesses serialize on precharge/activate. Stripe *concurrently-active* streams' clusters
   across *different* banks (slot→bank by physical address) and the controller overlaps one bank's activate
   with another's transfer — hiding exactly the latency §1.2 is made of. **Composes with the deferred
   priority-bucket slab** (`allocator_sdram_strangle.md` step 5): make those buckets bank-address-aware.
   Needs the slab to become physical-address aware (it isn't today).
2. **Read/write phase separation in the loader.** Render reads and loader DMA fills hit the one device
   concurrently (§1.4). Schedule the tail-fill DMA out of the render's peak read window (burst fills between
   render calls). Tractable — a timing change in `loader::pump` (`loader.cpp`).
3. **Multi-cluster coalesced reads.** Read 2+ clusters in one DMA *when the FAT chain is contiguous* — bigger
   sequential SDRAM write bursts + fewer SD seeks. But consecutive clusters usually *aren't* contiguous
   (the whole reason the design is cluster-based, `audio_file_manager.h`), so the win is **opportunistic**;
   needs bigger/variable slab slots or a scatter target. Moderate effort, conditional payoff.
4. **BSC timing-register tuning (CAS / refresh / burst)**, in deluge-sdk's `sdram.rs` — **largely already
   exhausted, low priority.** Synthstrom already tuned it (the C HAL's `bsc_userdef.c` noted: "WTRCD made quite a big difference
   / AC3L can't be reduced" — the CAS floor is hit). Row-thrash is a *pattern* problem, not a per-access-
   latency one, so little left here. One *untried* idea: **burst length** is set to 1 (`SDRAM_MODE = 0`) — a
   longer burst could improve cache-line-fill efficiency, but interacts with the A9 linefill/AXI behaviour and
   risks corruption if wrong. High destabilization risk, fully sim-blind. Note-and-defer.

**Sharper verification caveat than §6:** bank interleaving, read/write scheduling, and BSC tuning are *all*
hardware-timing, sim-completely-blind, and (4) can corrupt data if wrong. These need scope / CPU-meter
measurement, not just an ear test — higher-risk than the placement work.

---

## 6. Verification — read this before investing

**The host-sim cannot observe any of this.** It renders bit-identical audio whether a chunk sits in SRAM
or SDRAM, cached or uncached, and measures no cycles/bus contention. Every effect here (row-activate cost,
refresh stalls, bus serialization, cache behavior) is a **hardware-timing phenomenon**. Consequences:

- All of §4/§5 is **un-A/B-able offline** and must be validated on hardware — CPU meter / scope, listening
  for dropouts.
- Flag every firmware artifact **NEEDS-HARDWARE** before merge (house convention on these branches).
- **Repro to use:** the exact regressions — (B) a drum-heavy kit (many small samples triggered densely);
  (C) polyphonic multisampled synth stabs on a downbeat; (A) several long samples streaming at once.
- Sanity in the sim: `./dbt build debug` links; sim boots + render-smoke exit 0; `cargo test` +
  `ctest` green; clang-format/cargo-fmt clean. That proves *correctness*, not the perf win.

---

## 7. Open questions / risks

- **SRAM budget sizing.** How much of ~3 MB is actually free after everything else? Measure `sram_size()`
  headroom on a heavy song. Threshold *N* and the global cap depend on it.
- **Two-slab manager change** (Option 1) — scope of the Rust `deluge_resource` edit; keep the reclaim
  reentrancy guard intact (depth-1 retry in the C ABI).
- **Bank-aware placement feasibility** (§5b.1) — can the slab expose/choose a slot's SDRAM bank from its
  physical address, and is the 4-bank stripe granularity worth the slab
  complexity? Prereq for the biggest (A) win; measure the row-thrash cost first to size the prize.
- **Tier-2 demotion policy** — when exactly is a voice "idle" enough to demote its head? Avoid thrash on
  fast re-triggers (hysteresis / idle timeout, like the GrainBuffer idle-release precedent).
- **Eviction interaction** — SRAM-resident StreamedChunks are still reloadable; confirm the priority
  queues treat an SRAM-backed cluster's eviction the same (reload from SD) and don't assume SDRAM.
- **Metadata/`Sample` non-movable** — `SampleStream` holds a back-ref and is non-movable; placement
  changes must not perturb that.

---

## 8. Reference map

| What | Where |
|---|---|
| Chunk types | `src/deluge/storage/cluster/cluster.h` (`StreamedChunk`, `ComputedChunk`) |
| Per-sample stream orchestrator | `src/deluge/storage/audio/stream/sample_stream.{h,cpp}` |
| Loader pump / prefetch | `src/deluge/storage/audio/stream/loader.{h,cpp}` (`deluge::audio::stream::loader::pump`) |
| Backing = SDRAM slab (the seam) | `sample_stream.cpp:96`, `sample_cache.cpp:76` |
| Prefetch depth | `src/definitions_cxx.hpp:659` (`kNumClustersLoadedAhead = 2`) |
| Render read pointer (per-sample SDRAM deref) | `model/sample/sample_low_level_reader.cpp:44,69` |
| Head-cluster pinning (per SampleHolder, all referenced samples) | `model/sample/sample_holder.cpp` (`clustersForStart[]`, `setAudioFile`/`loadClusters`) |
| Slab/manager creation (one SDRAM slab) | `memory/general_memory_allocator.cpp:80,99-101` |
| Memory heap API | `src/deluge/memory/heaps.h` |
| Resource manager C ABI | `crates/deluge_resource/include/deluge_resource.h` |
| Slab pager | `crates/deluge_alloc/src/slab.rs`; manager `crates/deluge_resource/src/manager.rs` |
| Hardware: SDRAM part/config | Alliance `AS4C32M16SM-7TCN` (512 Mbit, 32M×16, 4-bank, CL2); config in deluge-sdk `crates/deluge-bsp/src/sdram.rs` — see §1 |
| Hardware: memory map + uncached mirror | deluge-sdk `crates/rza1l-hal/src/memmap.rs` |
| Hardware: cache mapping | deluge-sdk `crates/rza1l-hal/src/mmu.rs` (`PARA_NORMAL_CACHE`) |
| Prior allocator design docs | `allocator_sdram_strangle.md`, `allocator_redesign.md` (on `deepclone` et al., removed from `next`) |

---

## Suggested first move

Spike **Tier 1** (size-thresholded whole-small-sample → SRAM) as an SRAM-residency budget with priority
tiers baked in from the start (so Tier 2 drops in later), via Option 1 or a quick Option-2 prototype.
Measure SRAM headroom first (§7). Then hardware ear-test the drum-heavy-kit repro before anything else.
