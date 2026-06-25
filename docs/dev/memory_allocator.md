# The Deluge memory allocator: design, "stealables", and where it's going

This is a reference for the custom heap allocator (`src/deluge/memory/`) — what it is, the
unusual "stealable" mechanism, the latent 16-byte-alignment bug, why it matters for the
host simulator, and a proposed cleaner architecture. Written up while bringing the headless
host stem-renderer up on x86, which is where the alignment bug first became fatal.

## What the allocator is

It's a custom, bare-metal, single-threaded heap — *not* `malloc`. The hardware (RZ/A1L) has
**no MMU, no virtual memory, no swap, no OS** and a fixed ~64 MB SDRAM + small internal SRAM.

Key pieces:

- **`MemoryRegion`** (`memory_region.{h,cpp}`) — one contiguous arena managed as a free-list.
  Each allocation carries a **4-byte header** immediately before the user data and a **4-byte
  footer** after it (status bits + size). Adjacent free blocks **coalesce** on free. Empty
  spaces are tracked in an ordered array keyed by length (`emptySpaces`).
- **`GeneralMemoryAllocator`** (`general_memory_allocator.{h,cpp}`) — owns several
  `MemoryRegion`s and routes each request:
  - regions: `STEALABLE`, `EXTERNAL`, `EXTERNAL_SMALL` (all carved from the 64 MB SDRAM),
    `INTERNAL`, `INTERNAL_SMALL` (on-chip SRAM-equivalent).
  - `allocMaxSpeed` → internal first, then external, then stealable.
  - small vs large: within a region, allocations `> pivot_` grow from the *start* of a free
    space ("large"), `<= pivot_` from the *end* ("small"); reduces fragmentation.
- Region bounds come from the BSP via linker boundary symbols (`__sdram_bss_end`, `__heap_start`,
  `program_stack_start`, …) — on the host these are faked by `host_bsp.c` with static `.bss`
  arrays and `.set` directives. See `deluge_memory_region()`.

### Block layout / size math (important for alignment)

`padSize(n)` does `n += 8` (header+footer), rounds **up to a power of two** (clamped to
`minAlign_`=64 / `maxAlign_`), then returns `rounded - 8`. So **every `allocatedSize ≡ 8 (mod 16)`**,
and the per-block stride (`allocatedSize + 8`) is a multiple of 16.

## "Stealables" — the allocator is also the pager

A song references far more sample data than fits in RAM. Sample audio is streamed in as ~32 KB
**`Cluster`s** on demand, as a cache. Those caches are **stealable**: when the allocator is out
of free space, instead of failing it **evicts** a cold cache block and reuses the bytes — the
data can be re-read from the SD card later.

`Stealable` (`stealable.h`) is the eviction protocol:

```cpp
virtual bool mayBeStolen(void* thingNotToStealFrom) = 0; // safe to evict right now?
virtual void steal(char const* errorCode) = 0;           // evict + notify owner it's gone
virtual StealableQueue getAppropriateQueue() = 0;        // which LRU/priority queue
```

Subclasses are exactly the reloadable caches: `Cluster` (sample data / sample+perc caches),
`WaveTableBandData`, `GranularProcessor`. `steal()` calls back into the owner
(`sample->clusters[i]->cluster = nullptr`, `sampleCache->clusterStolen(...)`) so the engine
reloads from disk next time instead of reading freed memory.

**Why the allocator owns this:** on a desktop the OS page cache + swap evict cold pages under
pressure, transparently. Bare metal has none of that, so the allocator *is* the pager. When
`MemoryRegion::alloc` can't find space it calls `cache_manager_->ReclaimMemory(...)` →
`Stealable::steal()`. Hence the dedicated `STEALABLE` region, the `makeStealable` flag, and
`thingNotToStealFrom` (don't evict the cluster the calling voice is currently reading).

**The tradeoff this buys:** a single **unified pool** where caches and live objects share the
64 MB and caches *yield on demand*. The alternative — statically partitioning "X MB cache /
Y MB live" — wastes whichever partition is slack and causes spurious OOM. The coupling is a
deliberate scarce-RAM optimization, not just drift.

## The 16-byte alignment bug (latent on device, fatal on host)

`memory_region.cpp` *claims* "allocations are aligned to 16 bytes" but actually delivers
**8-byte** alignment: `regionBegin` is floored to 16 then `+16`, and the first user pointer is
`regionBegin + 8` → `≡ 8 (mod 16)`. The 8-byte block overhead is half of 16, so user pointers
sit on an 8-byte grid.

This is **undefined behaviour for any over-aligned type** (e.g. `alignof(TimeStretcher) == 16`
from a SIMD member; `SampleRecorder` embeds a `FIL` with `aligned(32)` DMA buffers). ARM/NEON
*tolerates* unaligned vector access, so it's silently fine on device. x86 **`movaps` faults**, so
it's a hard SIGSEGV on the host the moment such a type is constructed (TimeStretcher → time-stretched
sample playback, e.g. the "Glitchy_2" song).

### Why the fix is fiddly (parities)

For 16-alignment to *propagate* through every split and coalesce, two parities must hold:

1. First user pointer 16-aligned → `regionBegin ≡ 8 (mod 16)`.
2. **Every empty-space length `≡ 8 (mod 16)`**, because merge-left does
   `address -= (leftLength + 8)` — only `length ≡ 8 (mod 16)` keeps the merged address aligned.
   The initial length is `regionEnd - regionBegin - 16`, so `regionEnd` must be 16-aligned too.

Naive partial fixes (shift `regionBegin` only; re-align the small-path allocation by shrinking
its leftover) break parity #2 and *poison the free-list* — coalescing then yields 8-aligned
spaces and large allocations start failing the invariant elsewhere. **Lesson learned: do this
against `tests/32bit_unit_tests/memory_tests.cpp` with an "every returned pointer is 16-aligned"
invariant over randomized alloc/free sequences — not by render-testing a song and reading crash
tea-leaves.** The render loop is ~90 s; the unit test is ~1 ms.

### Status: basic path fixed, size-mutating paths still TODO

A three-line change in `memory_region.cpp` makes the **basic alloc / dealloc / coalesce** path
fully 16-aligned (the `MemoryAllocation.Alignment16` unit test churns 30k allocations with 0
misaligned):

1. `setup()`: `regionBegin = (regionBegin & ~15) + 24` (was `+ 16`) → user pointers ≡ 0 (mod 16).
2. `setup()`: `regionEnd = regionEnd & ~15` → initial empty-space length ≡ 8 (mod 16).
3. `padSize()`: `requiredSize = (requiredSize + 15) & ~15` before the `- 8` return → **every**
   allocation size is ≡ 8 (mod 16). (The old power-of-two rounding only landed on a 16-multiple
   for clean powers of two; sizes just over a `maxAlign` boundary, e.g. 4090, left a 1/2/4/8-byte
   remainder — *this* was the main reason the naive shift "didn't work".)

But the unit test only exercises plain `alloc`/`dealloc`. The **full allocator has more
size-mutating paths that don't yet preserve the ≡ 8 (mod 16) empty-space-length invariant**, so
the *sim* still produces 8-aligned allocations (e.g. TimeStretcher) and crashes. Confirmed via
instrumentation: `markSpaceAsEmpty()` is fed `size ≢ 8 (mod 16)` from the steal/reclaim leftover
(`CacheManager::ReclaimMemory` returns `stolenSize + amountsExtended`, which isn't normalized to
≡ 8) and that bad parity then spreads through coalescing into the `internal` and `small external`
regions. The remaining work is to make **every** empty-space-creating / size-setting site keep the
parity:

- the reclaim leftover in `alloc()` (`markSpaceAsEmpty(addr + allocatedSize + 8, reclaimedSize -
  requiredSize - 8, ...)`),
- `CacheManager::ReclaimMemory` / `attemptToGrabNeighbouringMemory` / `writeTempHeadersBeforeASteal`
  (the grab-and-extend machinery),
- audit `extendRightAsMuchAsEasilyPossible` and `shortenRight`/`shortenLeft` (these *look* correct
  but should get unit coverage).

The robust technique at each site is the same "absorb the parity excess into the allocation"
trick: when a leftover would be ≢ 8 (mod 16), shrink it to the nearest ≡ 8 boundary and grow the
allocation by the same few bytes so the block chain stays contiguous. **Next step: extend
`Alignment16` to also drive stealable allocations + `extend`/`shorten` so the remaining failures
are unit-visible, then fix each site against it.** This is a bigger, heap-corruption-sensitive
change than the three lines — treat it as its own carefully-tested piece (+ golden re-baseline +
ARM oracle), which is also the moment to weigh the host-specialized-allocator alternative again.

The host-only band-aid already shipped: gate the FATFS `aligned(32)` cache-line attribute off
under `DELUGE_HOST` (`ff.h` `FF_CACHE_ALIGN`). That fixes `SampleRecorder` but **not**
`TimeStretcher`, whose 16-alignment is its own SIMD member — only a real allocator fix covers it.

## Host vs device: keep one implementation (for now)

It is tempting to give the host a dead-simple `aligned_alloc`-backed allocator. Resist it, for now:
the host sim's core value is the **golden-master harness: host ≡ device, bit-exact** — that's what
lets x86 validate *device* DSP. A different host allocator means different addresses, different
allocation order, different layout; there are real pointer-value dependencies (the
open-addressing hash table keys on addresses) and, more importantly, the host has gigabytes so it
would **never hit the steal path** that the device hits under pressure mid-song — a behaviorally
different engine. Fix alignment in the **shared** allocator (it's a real device UB), re-baseline
goldens (addresses shift), validate on the ARM/QEMU oracle. Fork the implementation only *after*
the green golden harness can prove audio is pointer-value-independent — not as the thing allowed
to move the goldens.

## Proposed cleaner architecture (deliberate refactor, after the alignment fix)

The allocator currently does two unrelated jobs and the dependency runs the wrong way (a
low-level allocator reaches *up* into `Cluster`/`Sample`). The abstraction is half-there already
(`CacheManager` + the `Stealable` interface), so this is a **dependency inversion**, not a rebuild:

- **Allocator** becomes generic: `alloc(size, align, region) / dealloc(ptr)` plus a registered
  `reclaim(bytesNeeded) -> bool` callback it invokes when out of space. Knows nothing about caches.
- A **resource/buffer-pool layer** owns eviction policy: the `StealableQueue`s, LRU,
  `mayBeStolen`/`thingNotToStealFrom`, steal-then-notify. It *registers* the reclaim callback.
- App code (Sample/Cluster/SampleCache/WaveTable/Granular) talks only to the resource manager.

Same unified-pool behavior, but the arrows point down: `allocator → abstract reclaim hook ←
cache subsystem`. This is also the *right* port boundary for a host-specialized or Rust/embassy
allocator later — "generic allocator + registered reclaim hook" is far cleaner to swap than
"allocator that knows what a Sample is."

Caveats: the steal path is correctness- **and** timing-critical (a voice mid-read of a stolen
cluster is a glitch/crash — hence `thingNotToStealFrom`/`mayBeStolen`/priority queues). Treat the
inversion as a real, separately-validated subsystem refactor (unit tests + golden re-baseline),
**orthogonal to** and **after** the alignment fix.

## Sequence

1. **Alignment fix** in the shared allocator, driven by `memory_tests.cpp` (16-aligned invariant)
   → renderer works for time-stretch songs; re-baseline goldens; ARM-validate.
2. *(optional, later)* the `CacheManager`/`Stealable` **dependency inversion** as its own change.
3. *(optional, later)* a host-specialized or Rust allocator behind the clean API — only once the
   golden harness proves equivalence.
