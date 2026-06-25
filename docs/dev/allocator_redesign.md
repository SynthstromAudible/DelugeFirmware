# A from-scratch allocator design for the Deluge (Rust core, C API)

This is the forward-looking design for replacing the custom heap in `src/deluge/memory/`. It is the
concrete successor to the "Proposed cleaner architecture" section of
[`memory_allocator.md`](./memory_allocator.md) — read that first for *why* the current allocator is
also the pager, what "stealables" are, and the 16-byte alignment parity bug. This document assumes
that context and focuses on the replacement: what to build, in what order, and which invariants are
non-negotiable.

The target is a Rust `#![no_std]` core with a stable C ABI so both the C++ firmware and any future
Rust code can use it. Nothing here lands before the prerequisites in the last section are met.

## TL;DR

- It is **not one allocator**. Three allocation patterns get three structures, over two physical arenas.
- **TLSF** is the general-purpose backbone (O(1) bounded alloc *and* free, RT pedigree, alignment falls
  out of size classes). One instance per arena.
- **Slab pool** serves the fixed-size cache clusters and *is* the pager. Uniform slots make eviction O(1)
  and kill fragmentation, coalescing, and the parity bug in one move.
- The slab's backing blocks are **borrowed from and returned to** the SDRAM TLSF, so the unified-pool
  property ("caches yield on demand") is preserved without a static cache/live partition.
- **Target invariant: the RT path never allocates.** All allocation happens off the audio thread, so
  reclamation timing is never under the deadline. What remains is reentrancy correctness (depth-1 guard,
  sequenced steal) and pinning — the latter is what makes "RT only reads resident slots" sound.
- Rust buys us the alignment invariant *encoded once* so it can't be hand-derived wrong again. The one
  sharp edge is reentrancy across the FFI reclaim callback.

## Why three allocators, not one

The current `MemoryRegion` is ~888 lines with a parity landmine and the pager threaded through
`attemptToGrabNeighbouringMemory` because it serves three genuinely different patterns from a single
variable-size free-list. Segregate them and each becomes simple:

| Pattern | Today | Best structure | Why |
|---|---|---|---|
| Fixed-size churning caches (`Cluster`, ~32 KB) | general free-list + coalesce + steal woven in | **Slab pool** | O(1) alloc/free, zero fragmentation, **steal becomes trivial** |
| General variable-size, longer-lived objects | sorted `emptySpaces` array, power-of-two padding | **TLSF** | O(1) *bounded* alloc/free, alignment from size classes |
| Per-render-block transient scratch | (n/a / general) | **Bump arena, reset each block** | the "nursery" — wholesale reset, no per-object free |

"SLAB vs Nursery vs ..." is the wrong framing: the right design uses all of them, each for the traffic
it fits.

## Physical arenas: SRAM vs SDRAM

Two independent allocator instances, one per physical region. They must be separate — the address
ranges and per-access costs are genuinely different, and the BSP already hands us distinct boundary
symbols (`__sdram_bss_end`, `__heap_start`, `program_stack_start`, …; faked by `host_bsp.c` on host).

- **SRAM (fast, small) — TLSF instance.** Voice state, wavetable band data, hot DSP coefficients, the
  frame/bump arena, **and the allocator's own metadata** (free-lists, slab bitmaps — these must live in
  fixed static storage, the way `emptySpaces.setStaticMemory()` works today; no allocator-in-allocator).
  Placement policy mirrors today's `allocMaxSpeed`: hot-flagged requests try SRAM first, fall back to
  SDRAM.
- **SDRAM (slow, large) — TLSF instance** managing the bulk, **plus a slab pool layered on top** for the
  cache clusters (next section). Recording buffers and large/cold allocations go straight to this TLSF.

## The core idea: slab-backed-by-TLSF

This is the synthesis that keeps the unified pool *and* makes stealing O(1).

```
SDRAM TLSF  (generic: alloc / free / realloc; knows nothing about Samples)
   │  alloc/free of uniform ~32 KB blocks
   ▼
Slab cache pool  (owns LRU order, per-slot pin count, eviction)   ◄── registers reclaim(bytes)->bool
   │
   ▼
Cluster / WaveTableBandData / GranularProcessor  (talk only to the cache pool)
```

- The stealable cache is a **slab of uniform ~32 KB slots**, LRU-ordered.
- Slot backing is **requested from, and returned to, the SDRAM TLSF.** When playback warms up the slab
  grabs fresh blocks; when TLSF can't satisfy a *live-object* request it fires the registered
  **reclaim hook**, the slab evicts its coldest unpinned slot, returns that block to TLSF, and the live
  allocation proceeds.

What evaporates versus today: coalescing across stolen blocks, `attemptToGrabNeighbouringMemory`'s
neighbour-walk, power-of-two padding waste, and the empty-space parity bug. A steal is now: **pick
coldest unpinned slot → call its reclaim callback → return the slot.** Uniform size ⇒ O(1) and
unfragmentable.

This is the dependency inversion sketched in `memory_allocator.md`, made concrete: the arrows point
down (`allocator → abstract reclaim hook ← cache subsystem`); the allocator never reaches up into
`Cluster`/`Sample`.

### Why TLSF for the general backbone

- **Buddy:** power-of-two → up to 2× internal fragmentation. We already pay that with `padSize`'s
  power-of-two rounding; on a tight 64 MB budget it's the wrong trade.
- **Plain slab everywhere:** only works for fixed sizes; general objects are variable.
- **TLSF (two-level segregated fit):** `no_std`, no-MMU, hard-real-time, O(1) *worst-case* bounded alloc
  **and** free with bounded fragmentation — the textbook answer to "malloc/free with RT guarantees and
  no OS." Alignment is a property of the size class + arena base, so the hand-derived 16-byte parity
  that bites today stops being something we can get wrong.

## The frame (bump) arena — the "nursery"

For anything that lives only for the duration of one render block: a bump pointer in SRAM, **reset
wholesale at the block boundary**, no per-object free. This is the nursery idea without GC. In Rust the
arena can hand out `&'frame`-borrowed scratch the type system forbids from outliving the reset. Whether
the current DSP graph has such transient allocations is worth auditing; the pattern is cheap to provide
and O(1) by construction.

## Reclamation policy

"Should we prevent reclamation during alloc?" splits into three decisions with different answers.

### 1. Non-reentrant reclaimer — prevent, always

A single `reclaiming` flag. While the reclaim callback runs, any *nested* alloc that would itself want
to reclaim **fails fast** (returns null) instead of re-entering the reclaimer. Without this, a
cold-eviction that triggers another allocation recurses arbitrarily deep — and on a device with no MMU
guard page, stack overflow is silent corruption, not a clean crash. The guard caps reclaim at depth 1.

This does **not** forbid a reentrant `free()` during the callback — `steal()` is explicitly allowed to
free other blocks. It only forbids reentrant *reclaim*.

### 2. Sequence the steal — never hold the lock across the callback

You don't prevent the steal-time free; you order it, exactly as `writeTempHeadersBeforeASteal` does
today:

1. Claim the block you'll return; write consistent headers/footers so a reentrant `free()` sees a valid
   heap.
2. Drop any exclusive borrow (`&mut Heap`).
3. Call `reclaim` / `steal`.
4. Re-acquire, finalize.

In Rust this is the whole ballgame: no `&mut self` may be live across the callback boundary, or a
reentrant `free` is aliasing UB. Complete the free-list mutation, reach a consistent state, *then* call
out (interior mutability / claim-then-call).

### 3. The RT path never allocates (target state)

The end-state invariant is that **the audio thread never calls the allocator at all.** It only reads
clusters that are already resident and pinned (§4). All allocation — song load, voice buffers, *and the
staging of the next sample cluster* — happens off the audio thread. So there is exactly one allocation
context, and synchronous reclaim is always allowed there: it is never under the per-sample deadline.

This is strictly better than an "RT alloc is O(1) bounded" guarantee: you no longer have to *trust* a
worst-case allocator bound on the audio thread — you only have to *prove the path is allocation-free*,
which is enforceable. The transitional `deluge_alloc_rt` entry point (below) exists precisely to enforce
it: in debug builds it traps on any allocation/reclaim from the audio thread, turning "ideally" into
"asserted." Once the RT path is provably allocation-free, that entry point is purely a guardrail.

The hard deadline does not vanish, it softens. The off-thread loader must stage cluster N+1 (acquire a
slot, evict something cold, read from SD) before the RT thread finishes consuming cluster N; miss it and
you get a buffer **underrun** (a glitch), not a crash. So the **low/high-water proactive reclaimer**
survives — but demoted from a reclaim-*correctness* requirement to a **prefetch/lookahead depth** that
hides the loader's latency. It is one tunable (how many clusters ahead to stage), not a policy: keep N
slots staged ahead of playback; when free dips below low-water, evict coldest unpinned slots up to
high-water, off-thread.

### 4. Pinning — prevent reclamation of *specific* blocks

`thingNotToStealFrom` becomes a per-slot **pin/refcount**: a voice mid-read pins its slot; LRU eviction
(watermark or synchronous) skips pinned slots. This is a permanent property of the reclaimer,
independent of which path triggered it.

### The tradeoff to keep honest

The unified pool still has to actually yield: a sudden large off-thread allocation (a voice's buffers, a
song load) must be able to reclaim memory that cold caches are squatting on, or you have silently
re-introduced the static cache/live partition `memory_allocator.md` argues against. Off the RT thread
that is fine — synchronous sequenced steal (per §1/§2) handles the bursty demand, and the watermark
prefetcher handles the steady-state streaming demand. Both, but both now off the audio thread.

## The C ABI

Handle-based, in the `libdeluge` namespace (`include/libdeluge/memory.h`), header auto-generated with
`cbindgen`. A `DelugeHeap` is built over the BSP's existing region description (`DelugeMemoryRegion` /
`DelugeMemoryKind`), so placement (fast SRAM vs slow SDRAM) is a property of *which heap* — there is no
`allocMaxSpeed`/`allocLowSpeed` split. See the implementation plan's "API strategy" and "Relationship to
the BSP, libdeluge, and embassy" sections.

```c
typedef struct DelugeHeap DelugeHeap;

// The single real entry point: always off the audio thread, may reclaim, sequenced, depth-1.
void* deluge_alloc(DelugeHeap*, size_t size, size_t align);
void  deluge_free (DelugeHeap*, void* ptr);

// Guardrail (see policy §3): debug builds trap if called from the audio thread, enforcing the
// "RT path never allocates" invariant. Not a separate allocation strategy.
void* deluge_alloc_rt(DelugeHeap*, size_t size, size_t align);

// Cache pool registers its eviction hook; allocator stays generic.
void  deluge_heap_register_reclaim(DelugeHeap*, bool (*cb)(void* ctx, size_t bytes), void* ctx);

// Pinning for the steal-safety invariant (thingNotToStealFrom).
void  deluge_pin  (DelugeHeap*, void* ptr);
void  deluge_unpin(DelugeHeap*, void* ptr);
```

Rust core notes:

- `#![no_std]`; implement `core::alloc::{GlobalAlloc, Layout}` so it can back Rust `alloc` collections
  later.
- Encode the alignment invariant **once** in a typed `Layout`/size-class so the parity bug becomes
  unrepresentable.
- All allocator metadata in fixed static storage — no `Vec`/`Box` inside the allocator's own guts.
- The reclaim callback is reentrant and crosses FFI back into C++; §1 and §2 are mandatory, not
  optional.

## Invariants any implementation must preserve

- **The audio path never allocates.** This is the primary RT guarantee — stronger and more enforceable
  than "alloc is O(1) bounded." The audio thread only reads resident, pinned slots. TLSF + slab still
  give worst-case bounds for the off-thread allocator, but the RT thread never depends on them.
- **16-byte alignment** for over-aligned SIMD types (`TimeStretcher`, `SampleRecorder`'s DMA buffers).
  See the parity discussion in `memory_allocator.md`.
- **Steal safety:** never evict a pinned/`thingNotToStealFrom` slot; consistent heap state before any
  reclaim callback.
- **Unified pool:** caches yield on demand; no static cache/live partition.
- **Single-threaded** today. If a second core or DMA producer ever shares a heap, the handle makes
  locking explicit — but the C++ today assumes no locks, so don't add them speculatively.

## Prerequisites and sequencing

This is gated by two constraints written down in `memory_allocator.md` — repeated here because they
decide *when* this lands, not *what* it is.

1. **Fix 16-byte alignment in the *current* shared allocator first**, driven by
   `tests/32bit_unit_tests/memory_tests.cpp` with a "every returned pointer is 16-aligned over
   randomized alloc/free sequences" invariant. This unblocks time-stretch songs on host now, and the
   same randomized-invariant harness becomes the **conformance test** for the Rust allocator later.
2. **Golden-master determinism.** A different allocator changes addresses, allocation order, and — on a
   host with gigabytes — never exercises the steal path, making it a behaviorally different engine. So
   this redesign lands only *after* the green golden harness proves the audio is
   pointer-value-independent (the open-addressing hash table keys on addresses). Do not let the new
   allocator be the change that moves the goldens.

Suggested order:

1. Alignment fix + 16-aligned invariant test in the existing allocator. Re-baseline goldens; ARM/QEMU-validate.
2. `CacheManager`/`Stealable` → reclaim-hook dependency inversion in the existing C++ (still C++), as its
   own separately-validated change.
3. Prove pointer-value-independence via the golden harness.
4. Rust core behind the C ABI: TLSF arenas, slab pool, frame arena, reclamation policy. Validate against
   the randomized invariant test and the golden harness.
