# Allocator rewrite — implementation plan

The staged plan for executing the design in [`allocator_redesign.md`](./allocator_redesign.md). Read that
first (the *what* and *why*); this is the *how* and *in what order*. Background on the current allocator,
the stealable/pager coupling, and the alignment bug is in [`memory_allocator.md`](./memory_allocator.md).

## Context

The current allocator (`src/deluge/memory/`) does two jobs at once — general heap *and* sample-cache
pager — with the dependency running the wrong way (`MemoryRegion::alloc` calls up into
`CacheManager::ReclaimMemory`, which calls into `Cluster`). It also delivers only 8-byte alignment, which
is UB for over-aligned SIMD types and a hard SIGSEGV on the x86 host. The in-flight attempt to patch
16-byte alignment into the existing free-list proved architecturally baked-in and fragile (parity must
propagate through every split/coalesce). **Decision: a grounded, tested rewrite is the clean path** — the
new TLSF+slab design gets alignment right by construction, so the rewrite *is* the alignment fix rather
than a separate patch.

End goal: a Rust `#![no_std]` allocator core (TLSF arenas + slab-backed cache pool) behind a stable C ABI,
with the RT/audio path provably allocation-free.

## The migration scaffold (temporary) vs the destination API

The `GeneralMemoryAllocator` surface is a **migration scaffold, not the endpoint.** We forward it to the
new core so the rewrite lands without touching hundreds of call sites at once — then strangle it. The
*destination* is the `libdeluge` C ABI (next section), with typed C++ allocators on top. Do not treat the
GMA signatures as a contract to keep forever; they bake in conflations the redesign exists to remove.

Held stable **only during migration**, then deprecated:

- **`GeneralMemoryAllocator` public methods** — `allocMaxSpeed` / `allocLowSpeed` / `allocStealable` /
  `allocExternal` / `allocInternal` / `dealloc` / `shortenRight` / `extend` / etc.
  (`general_memory_allocator.h`). ~80 call sites route through these; they become thin forwarders to the
  clean ABI, then get strangled (see "API strategy").
- **std-allocator adapters** — `fast_allocator.h`, `sdram_allocator.h`, `external_allocator.h`. These are
  the *keepers*: idiomatic C++ and the right long-term surface. Repoint them at the clean ABI early.
- **C ABI** — `delugeAlloc` / `delugeDealloc` (`memory_allocator_interface.h`) → subsumed by `deluge_alloc`.

What gets replaced/inverted: `MemoryRegion` internals, `CacheManager`'s `ReclaimMemory` +
neighbour-grabbing, and the `Stealable` coupling.

## API strategy: libdeluge ABI as destination, GMA as deprecation shim

The clean API is not greenfield — it extends the **existing `libdeluge` C boundary**
(`include/libdeluge/memory.h`), which already models memory as board-provided regions:
`DelugeMemoryRegion { void* base; uint32_t size; DelugeMemoryKind kind; }` + `deluge_memory_region(index,
out)`. The BSP (`src/bsp/{host,rza1}/`) provides the regions; the allocator consumes them. The new
allocation entry points (`deluge_alloc` etc.) live in that same namespace.

The GMA surface bakes in four conflations the clean ABI removes:

| GMA encodes... | ...as | Clean form |
|---|---|---|
| placement (fast/slow) | method name (`allocMaxSpeed`/`allocLowSpeed`/`allocExternal`/`allocInternal`) | **region/kind handle** — `DelugeMemoryKind` already models this |
| "reclaimable cache" | alloc flag (`allocStealable`) | **separate cache-pool API** — stealables leave the general allocator entirely |
| steal-protection | per-call param (`thingNotToStealFrom`) | **pin op** on the cache pool |
| resize | bespoke `shortenLeft/Right` / `extend*` | `realloc(ptr, size, align)` |

Migration tactic (strangler, not big-bang):

1. Define the clean ABI; implement the new core behind it.
2. Repoint the 3 std-allocator adapters + `allocate_unique.h` at the clean ABI — this moves the bulk of
   call sites with a handful of edits.
3. Keep GMA methods as forwarders for the remaining direct `GeneralMemoryAllocator::get()` callers.
4. Strangle those direct callers in a later pass; delete GMA. **Not a blocker for the core rewrite.**

## Phases

### Phase 0 — Conformance harness + golden gate (foundation; do first)

This is what makes the rewrite "grounded and tested." No allocator code changes here.

- Extend `tests/32bit_unit_tests/memory_tests.cpp` into an **allocator-agnostic conformance suite** behind
  a thin test interface (`alloc(size, align)` / `free` / `realloc`), so the *same* randomized tests run
  against the old `MemoryRegion` and the new core. The file already has the right shape
  (`RandomAllocFragmentation`, `uniformAllocation`, `stealableAllocations`, and an `Alignment16` test at
  line 313) — generalize, don't restart.
- Invariants asserted over long randomized alloc/free/realloc sequences: **every returned pointer is
  16-aligned** (and honors requested align); no overlap; headers/footers consistent; coalescing correct;
  reclaim returns usable space; no leak after full drain.
- **Golden-master gate:** capture the current host↔device golden baseline and decide the
  pointer-value-independence question (the open-addressing hash table keys on addresses). Any later phase
  that shifts addresses re-baselines goldens and ARM/QEMU-validates. Treat this as the merge gate for
  Phases 2–4.

### Phase 1 — Invert the cache/stealables boundary (C++, behavior-preserving)

Make the allocator generic; move eviction policy into a resource layer. Old `MemoryRegion` free-list stays
underneath, so goldens should not move (pure refactor — validate that they don't).

- Introduce a **`Reclaimer` hook** the allocator calls when out of space:
  `bool reclaim(size_t bytesNeeded, void* dontReclaimFrom)`. `MemoryRegion::alloc` calls this instead of
  `cache_manager_->ReclaimMemory(...)` directly. The allocator no longer includes `stealable.h` /
  `cache_manager.h`.
- Move `CacheManager` behind that hook as the first `Reclaimer` implementation: it keeps the
  `StealableQueue` LRU lists and registers `reclaim`. App code (`Cluster`, `SampleCache`, `WaveTable`,
  `Granular`) talks to the resource layer, not the allocator.
- Files: `memory_region.{h,cpp}`, `general_memory_allocator.{h,cpp}`, `cache_manager.{h,cpp}`,
  `stealable.h`. See "New boundary" below for the interface shapes.
- **Verify:** full conformance suite green; golden render unchanged (this phase must be a no-op on output).

### Phase 2 — Move cluster-slot acquisition off the RT path (behavioral)

Today the audio thread allocates: `voice_sample.cpp` (≈202, 847) and `sample_low_level_reader.cpp`
(≈305, 382) call `getCluster(…, CLUSTER_ENQUEUE)` → `Cluster::create()` → `allocStealable()`, which can
trigger a synchronous steal **on the render thread**. The SD *read* is already deferred
(`loadAnyEnqueuedClusters`, a repeating task in `deluge.cpp:523`); the *slot allocation + steal* is not.

- Replace audio-path `Cluster::create()` calls with a **request/enqueue that does not allocate**. The
  audio thread only reads slots that are already **resident and pinned**; on a miss it enqueues and
  outputs the graceful fallback (silence/last-sample) for that cluster — an underrun, not a crash.
- The off-thread loader (`audio_file_manager.cpp::loadAnyEnqueuedClusters`, `loadingQueue`) does slot
  acquisition + steal + SD read.
- Introduce the **slab pool of uniform ~32 KB cluster slots** with a **low/high-water prefetcher**: keep N
  slots staged ahead of playback; evict coldest unpinned slots off-thread to refill. N (lookahead depth)
  is the one tunable replacing the RT steal.
- **Pinning:** `Cluster::numReasonsToBeLoaded` becomes the slot pin count (pin/unpin); eviction skips
  pinned slots — this is what makes "RT reads only resident slots" sound.
- Add the **debug guardrail**: `deluge_alloc_rt` / an assertion that traps on any allocation or reclaim from
  the audio thread, so the invariant is enforced, not hoped for.
- **Verify:** conformance green; re-baseline goldens (voice-culling timing may shift); ARM/QEMU-validate;
  stress-test memory-pressure songs (the `numRefusedTheft >= 512` / `bypassCulling` path) for underruns.

### Phase 3 — New allocator core: TLSF + slab-backed-by-TLSF (Rust, behind the C ABI)

Replace `MemoryRegion`/`GeneralMemoryAllocator` internals with the real design. Alignment is correct by
construction (size classes + aligned arenas) and proven by the Phase 0 suite.

- **Rust crate** (`#![no_std]`), new top-level (e.g. `rust/deluge_alloc/`):
  - **TLSF** general allocator, one instance per arena (SRAM, SDRAM). O(1) bounded alloc/free; metadata in
    fixed static storage (no `Vec`/`Box` in the allocator's own guts).
  - **Slab pool** of uniform cluster slots whose backing blocks are alloc'd from / freed to the SDRAM
    TLSF, with LRU + pin counts + the watermark prefetcher (the Phase 2 logic, now in Rust).
  - **Reclamation policy:** depth-1 non-reentrant guard; sequenced steal (no `&mut self` held across the
    reclaim callback — claim block + write consistent headers, drop borrow, call out, finalize); reentrant
    `free()` during the callback is fine.
  - Implement `core::alloc::{GlobalAlloc, Layout}` so it can back Rust `alloc` collections later.
- **C ABI** in the `libdeluge` namespace via `cbindgen`-generated header (see "C ABI" below). The C++
  `GeneralMemoryAllocator` methods become thin forwarders (migration scaffold); the std-allocator adapters
  are repointed at the clean ABI and are the keepers.
- **Build integration** (greenfield — no Rust in the tree today; shared with the BSP/embassy Rust
  substrate, see "Relationship to..."): Cargo + CMake via `corrosion-rs`; `cbindgen` in the build; host
  target `x86_64-unknown-linux-gnu` (`no_std`, custom global, panic=abort).
- **Verify:** Phase 0 suite against the Rust core (host); re-baseline goldens; bit-exact host render vs
  the Phase 2 device baseline before touching device.

### Phase 4 — Bring the Rust core to device; retire the old allocator

- Cross-compile for the RZ/A1L (Cortex-A9 / `armv7a-none-eabi` or a custom target), bare-metal: panic
  handler, no unwinding, link against BSP boundary symbols (`__sdram_bss_end`, `__heap_start`,
  `program_stack_start`).
- Validate on the **ARM/QEMU oracle**; full golden re-baseline on device.
- Delete `memory_region.{h,cpp}`, the old `CacheManager` neighbour-grab code, and `Stealable`'s
  allocator-facing bits; keep only the resource-layer interfaces.

## The new cache/stealables boundary (detail)

The arrows invert: `allocator → reclaim hook ← cache pool`. Concrete mapping from today's `Stealable` /
`CacheManager` to the new slab model:

| Today | New | Notes |
|---|---|---|
| `MemoryRegion::alloc` → `CacheManager::ReclaimMemory` | allocator → registered `reclaim(bytes, dontReclaimFrom)` | allocator stops including `stealable.h` |
| `CacheManager` neighbour-grab + `longest_runs` + `lastTraversalNo` | **deleted** | uniform slots ⇒ no neighbour-walking, no run tracking |
| `Stealable::steal()` | slot **eviction callback** (notify owner: `sample->clusters[i] = nullptr`) | same effect, owned by the pool |
| `Stealable::mayBeStolen(thing)` | slot **pin count == 0** (+ optional predicate) | pinning is primary |
| `Stealable::getAppropriateQueue()` | slab **LRU bucket classifier** | keep the queue priorities from `Cluster::getAppropriateQueue` |
| `Cluster::numReasonsToBeLoaded` | slot **pin/unpin** | the load-bearing RT-safety invariant |
| `allocStealable(size)` on RT path | off-thread slot acquire + watermark prefetch | Phase 2 |

Other `Stealable` subclasses — `WaveTableBandData` (`wave_table.cpp:270`) and `GrainBuffer`
(`GranularProcessor.cpp:307`) — are not fixed-size cluster traffic; they keep using the general allocator
with the reclaim hook (registered as reclaimable owners), and do **not** go through the cluster slab.

## The C ABI (detail)

Lives in the `libdeluge` namespace (`include/libdeluge/memory.h`), header generated by `cbindgen`. A heap
is constructed from the BSP's existing region description (`DelugeMemoryRegion` / `DelugeMemoryKind`), so
placement is a property of *which heap*, not a method name:

```c
typedef struct DelugeHeap DelugeHeap;

// Heaps are built over BSP regions (deluge_memory_region / DelugeMemoryKind: fast SRAM vs slow SDRAM).
// Placement = which heap you allocate from; no allocMaxSpeed/allocLowSpeed split.
void* deluge_alloc  (DelugeHeap*, size_t size, size_t align);   // off-RT; may reclaim; sequenced; depth-1
void  deluge_free   (DelugeHeap*, void* ptr);
void* deluge_realloc(DelugeHeap*, void* ptr, size_t new_size, size_t align);

void  deluge_heap_register_reclaim(DelugeHeap*, bool (*cb)(void* ctx, size_t bytes, void* dont_reclaim_from), void* ctx);
void  deluge_pin  (DelugeHeap*, void* ptr);   // replaces thingNotToStealFrom / numReasonsToBeLoaded
void  deluge_unpin(DelugeHeap*, void* ptr);

void* deluge_alloc_rt(DelugeHeap*, size_t size, size_t align); // guardrail: debug build traps if called from audio thread
```

During migration, `GeneralMemoryAllocator::allocMaxSpeed/allocLowSpeed/allocExternal/dealloc` forward to
these (SRAM-first heap / SDRAM heap) — the scaffold, later strangled. The std-allocator adapters
(`fast_allocator.h` → SRAM heap, `sdram_allocator.h`/`external_allocator.h` → SDRAM heap) are repointed
here early and are the long-term C++ surface.

## Relationship to the BSP, libdeluge, and embassy

- **The allocator is downstream of the BSP, not part of it.** The BSP (`src/bsp/{host,rza1}/`) already
  answers "what physical regions exist and of what kind" via `deluge_memory_region()` / `DelugeMemoryKind`
  — and that kind field *is* the SRAM/SDRAM (fast/slow) signal the new arenas need. The allocator consumes
  that; it does not own region discovery. No new BSP surface is required, only consumption of the existing
  one.
- **Peer of embassy, not a dependency on it.** Embassy is an async executor and does **not** provide a
  heap; embedded Rust supplies a separate `#[global_allocator]`. So the allocator and embassy are peers
  sitting on the BSP, both `no_std`. The allocator needs none of embassy's async machinery.
- **What they share is the substrate, and that is the real coupling.** The risky, net-new work — two
  `no_std` Rust targets (host `x86_64`, device `armv7a`), panic handler, no-unwind, linker integration
  against BSP boundary symbols, `corrosion` + `cbindgen` — is identical to what any Rust-BSP/embassy effort
  must stand up. Land that substrate **once**, jointly. Sequence the allocator right after it; if the
  embassy effort is far off, the allocator can stand up the *minimal* substrate itself (smaller than a full
  embassy port) and the embassy work inherits it.
- **Synergy goal:** the Rust core implements `core::alloc::GlobalAlloc`, so the *same* heap can back
  embassy's `#[global_allocator]` and the firmware — one heap, both consumers. Make this an explicit design
  goal, not an afterthought.

**Net:** couple to the BSP region API (exists) and to the shared Rust/`no_std` toolchain bring-up; do
**not** gate on embassy's async runtime. The allocator is the first real payload that justifies — and
forces — that shared substrate.

## Verification summary

- **Per phase:** the Phase 0 conformance suite (16-aligned invariant over randomized sequences) must stay
  green — it is the unit-level oracle. Run via the existing `tests/32bit_unit_tests` harness.
- **Phases 1:** golden render **unchanged** (no-op refactor).
- **Phases 2–4:** re-baseline host goldens, then ARM/QEMU-validate bit-exact; stress memory-pressure songs
  (time-stretch e.g. "Glitchy_2", and the high-steal-pressure single-long-sample case) for underruns and
  for the previously-fatal `TimeStretcher` 16-alignment path.
- **Device (Phase 4):** full on-hardware/QEMU golden re-baseline before deleting the old allocator.

## Risks / open questions

- **Pointer-value dependence (golden harness).** Must be settled in Phase 0 or every later phase fights it.
  If the engine isn't pointer-value-independent, the goldens move on every address shift and can't be a
  pure regression oracle — they become "re-baseline + ARM-validate" each time.
- **Audio-path allocation audit.** Phase 2 assumes the only RT allocations are cluster slots. Confirm by
  tracing all `getCluster(CLUSTER_ENQUEUE)` / `allocStealable` / container-growth (`resizeable_array`,
  `open_addressing_hash_table`) calls reachable from the render path; any other RT allocator call must be
  hoisted off-thread or pre-reserved.
- **Reentrancy across FFI.** The reclaim callback crosses into C++ and may free other blocks mid-alloc;
  the depth-1 guard + claim-then-call sequencing are mandatory, and the hardest part to get right in Rust.
- **Two Rust targets, greenfield toolchain.** Host (`x86_64`) and device (`armv7a`) `no_std` builds, panic
  handlers, and CMake/Cargo integration are net-new infra (no Cargo.toml in the tree today).
- **Slot size coupling.** Cluster slab slot size is tied to `Cluster::kSizeFAT16Max` (32 KB) + cache-line
  padding; confirm the slab honors the `CACHE_LINE_SIZE` start offset the `Cluster` layout requires.
