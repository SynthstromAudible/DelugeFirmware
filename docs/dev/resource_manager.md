# Resource / cached-asset manager — design (first principles)

Status: **design sketch** (not started). Successor to the `Stealable` / `CacheManager` / `numReasonsToBeLoaded`
machinery that currently lives in `src/deluge/memory/` and `AudioFileManager`. Gated behind the allocator
rewrite landing (done: `deluge_alloc` is the allocator; the `GeneralMemoryAllocator` is now a thin
stealable/reclaim *coordinator* facade — see `allocator_sdram_strangle.md`). This manager is where that
coordinator's substance (the `CacheManager` + cluster slab + reclaim policy) finally belongs; when it lands,
the GMA class disappears as a side effect.

## What this system fundamentally is

A **bounded pool of RAM holding reconstructable assets**: keep the most valuable resident, rebuild the rest
on demand. That's a cache with three *orthogonal* mechanisms the current design conflates:

1. **Reconstruction** — how do I re-materialize bytes I evicted?
2. **References / leases** — who is using this *right now*, and who *cares about* it?
3. **Value / eviction policy** — of the evictable assets, which dies first?

The byte allocator (`deluge_alloc`) is **not** part of this — it is the mechanism underneath, driven via the
reclaim hook. This manager owns the *residency budget* within the unified SDRAM pool; the allocator owns
fragmentation/placement.

```
  voices / UI / loader        ← callers
        │  take/release leases, request assets
        ▼
  ┌─────────────────────────┐
  │  Resource Manager        │  identity · references · value · lifecycle · prefetch
  │   Asset → Chunk model    │
  │   Source (reconstruct)   │──→ filesystem / DSP recompute / zero-fill
  └─────────────────────────┘
        │  alloc/free chunks, register reclaim hook
        ▼
  deluge_alloc  (TLSF + cluster slab)   ← pure mechanism
```

## Primary vs derived state

The single biggest simplification: most of what the current code stores (type, priority, queue, "current
song"-ness, `mayBeStolen`) is **derivable**. Store the inputs, compute the rest.

**Irreducible — stored per asset:**

| Field | Meaning |
|---|---|
| **reconstruction key + recipe** | the *only* truly fundamental field. File asset → `path (+ byte range/format)`; derived asset → `(source ref + transform params)`; scratch → "zero-fill". **path and type are facets of this.** |
| **hard-lease count** | active readers that make it non-evictable (today's `numReasonsToBeLoaded`) |
| **soft references** | who *cares* but tolerates a reload (the song that uses it) — count or back-edge into the project graph |
| **residency** | `{state, ptr, size}` when resident; the state enum |
| **dirty + writeback target** | recordings: bytes that are the *only* copy → "reconstruction cost = ∞ until flushed" |
| **recency datum** | one tick stamp for the value function |

**Derived — computed, never stored:**

| Quantity | Derived from |
|---|---|
| **type** (sample/wavetable/cache/perc-cache) | the recipe + access pattern |
| **priority / "which of the 10 queues"** | the value *score* (below) |
| **`mayBeStolen`** | `leases == 0 && !dirty && reconstructable` |
| **"current song" vs "no song"** | does the project graph hold a soft reference? (reachability, not a flag) |
| **size when non-resident** | the recipe (file length / param math) |

`getAppropriateQueue()` — today hand-coded and scattered across `Cluster`/`SampleCache` — collapses into
"store the inputs, compute the score."

## Entity model: two-level Asset → Chunk

The current cache is mixed-granularity — per-**cluster** for samples (only the playing window of a 100 MB
sample need be resident), whole-asset for wavetables / bands / grain buffers. Unify:

- **Asset** — identity (reconstruction key), Source, references (hard + soft), dependencies, recency.
- **Chunk** — the resident, individually-leased/evicted unit. Atomic assets are single-chunk.

This unifies "stream a sample's clusters" and "hold a whole wavetable" under one structure, and it's where
**streaming/prefetch** lives:

- sequential read-ahead (load chunk N+1 while playing N),
- a bounded resident **window** per active stream (release-behind chunk N−2),
- the lease is on the *window*, not the whole asset.

That window + prefetch is the deferred RT-prefetcher.

## Reconstruction — the Source abstraction (the heart)

Every asset is cacheable *because it can be regenerated*; the kinds differ only in **how**:

| Asset | Reconstruction | Cost class | Depends on |
|---|---|---|---|
| sample cluster | `read(file, offset, len)` | I/O | — |
| repitched / perc cache | `recompute(sourceSample, params)` | CPU | source sample resident |
| wavetable band | `recompute(baseWavetable)` | CPU | base wavetable |
| grain buffer | zero-fill | free | — |
| **recording (dirty)** | none — bytes are the only copy | ∞ until flushed | writeback target |

Requirement: a `Source` interface — `materialize(key, dest)` + a declared **cost** + its **dependencies**.
Dependencies matter for the wavetable-band → whole-wavetable cascade and the cache → sample edge: keep the
source warm while a derived asset is hot, and know reconstruction prerequisites.

## References: hard leases vs soft references

Today there are *three* pin mechanisms — `numReasonsToBeLoaded`, the transient `thingNotToStealFrom`, and
"is it in the current-song queue." Collapse to two strengths:

- **Hard lease** — an RAII handle a reader holds; eviction skips it. (`thingNotToStealFrom` is just a
  degenerate single-op lease taken during one allocation.)
- **Soft reference** — the project/song holds these on assets it uses; they only raise priority. Eviction
  is allowed; reload on next access.

So **relevance becomes "just another reference"** — no special "current song" concept; the manager asks the
project model what's referenced (ties into `project_model` extraction).

## Eviction value function (replaces the 10 static queues)

```
value(X) ≈ P(used again soon) × cost_to_reconstruct(X) ÷ size(X),  with leased||dirty ⇒ never
```

Inputs: **relevance** (soft refs), **reconstruction cost** (free / I/O / CPU — the real reason the taxonomy
exists), **recency/frequency** (real usage metrics; today only crudely approximated by queue position +
`lastTraversalNo`), **size**. The 10 `StealableQueue`s are a coarse bucketing of (relevance × kind) — keep
buckets for O(1) victim selection if desired, but **derive the bucket from the score**, don't hand-assign it.

## Lifecycle

`Known → Loading → Resident → Resident+Leased → (Dirty) → Evicting`. Cases the current system special-cases
and the new one must keep:

- **Dirty/unsaved** (recording) — non-evictable until flushed.
- **Reconstruction failure** (card pulled mid-reload) — the `AudioFileManager::cardReinserted` "mark
  unplayable / cluster-size-changed" path; a defined "source vanished" transition.
- **Graceful OOM** when everything is leased (today's "all pinned → fail, don't crash").
- **Anti-thrash guard** (today's "refused 512× → `bypassCulling`").

## The RT contract (the constraint that shapes everything)

- **Audio/RT thread:** only takes/releases leases on already-resident chunks and reads them. Never
  allocates, never does I/O, never evicts.
- **Loader/manager (off-RT):** all I/O, allocation, eviction, prefetch decisions.

Today the RT path *can* allocate/load/evict (descoped — see `rt_allocation_audit.md`). The new design need
not *enforce* RT-no-alloc immediately, but must **not preclude** it. Prefetch is what makes it achievable:
keep the next chunks resident+leased ahead of the playhead so the RT path always finds them.

## Boundaries (out of scope; the seam to each)

- **Allocator** (`deluge_alloc`) — bytes, fragmentation, slab. The manager registers
  `deluge_heap_register_reclaim(hook, ctx)`; that one hook is the entire allocator-side seam.
- **Filesystem / SD driver** — the backend behind file-kind Sources.
- **Project model** — the soft-reference graph ("what does the song use").
- **Plugins / foreign assets** — per `extensibility_*`: a CLAP-ish instrument bringing its own samples wants
  to register a Source, so the Source interface should be public-shaped.

## What collapses vs today

- `Stealable` (3 virtuals) + `CacheManager` (10 queues + `reclaimOne` + traversal numbers) +
  `numReasonsToBeLoaded` + `thingNotToStealFrom` + `getAppropriateQueue` scattered across subclasses
  → **one Asset/Chunk model + a Source interface + a value function + leases.**
- The "stealable by accident" set goes away: grain buffers (non-shared scratch) and unsaved recordings stop
  masquerading as reloadable sample clusters — they're plain allocations or dirty-non-evictable *by their
  recipe*, not by ad-hoc queue assignment.
- Priority stops being hand-maintained; it's computed from stored inputs.
- The eviction policy stops living in `memory/` while reading audio-domain state (the layering inversion) —
  it moves to where the domain knowledge is.

## Migration path (incremental, behavior-preserving where possible)

1. Introduce `Source` + the Asset/Chunk model alongside the existing system; back the current `Cluster` with
   it for sample streaming first (highest-traffic, already slab-backed).
2. Move soft-reference / relevance onto project-held references (needs `project_model`).
3. Fold `CacheManager`'s `reclaimOne` into the manager's value-based eviction; it registers the reclaim hook
   instead of the GMA. **GMA coordinator dissolves here.**
4. Migrate wavetable bands, perc/repitched caches, then retire `Stealable` / `CacheManager` /
   `numReasonsToBeLoaded`.
5. (Later, gated on the synth rework) enforce the RT contract via the prefetcher.

## Decisions (resolved 2026-06-21)

1. **Relevance lives in the project model.** The project/song holds **soft references** on the assets it
   uses; the manager has no separate "current song" set — relevance is reachability in the project graph.
   (Depends on `project_model` extraction; until that exists, a thin shim can hold the soft-ref set.)
2. **Eviction is a computed value score**, not hand-assigned categories. Coarse buckets are allowed only as
   an *implementation detail* of indexing that score for fast victim selection — never as domain truth (the
   way today's 10 `StealableQueue`s are). Inputs as in the value-function section.
3. **RT contract: design-for-but-don't-enforce (yet).** The Asset/Chunk/lease model assumes the split
   (RT only leases resident chunks; loader does I/O/alloc/evict off-RT) and the prefetcher slots in cleanly,
   but enforcement (RT path provably allocation-free) waits on the synth/engine rework. Don't *preclude* it.
4. **The `Source` interface is a public extension point** — not internal-only. External code (plugins /
   foreign instruments per `extensibility_*`, and the `libdeluge` boundary) can register Sources and bring
   their own cached assets. **Implication:** the boundary must be foreign-code-friendly (C-ABI-shaped, stable,
   versionable), and the manager treats asset kinds **opaquely** — it only knows `{key, materialize, cost,
   dependencies, size}`. The built-in kinds (sample / cache / wavetable / band) are simply the first Sources
   implementing that interface, with no special-casing. This *reinforces* the "derive everything" design:
   the manager structurally cannot hand-code per-kind logic, because it doesn't know the kinds.

## Storage integration: blocks vs chunks vs slabs (decided 2026-06-21)

Keep three granularities distinct — let none bleed into the manager:

- **Block / sector (512 B)** — the SD/FatFS I/O unit. Lives *only* inside a Source's `materialize`,
  reading via `libdeluge/block_device.h`. The manager never sees sectors.
- **Chunk = FAT cluster (~32 KB)** — the manager's residency/eviction unit (today's `Cluster`). The right
  granularity: 512 B chunks would be 64× the table entries + acquire/materialize traffic for nothing.
- **Slab slot** — the *backing* for uniform clusters.

So a sample is an **Asset**, its clusters are **Chunks**, and `materialize(owner, index, dest, len)` = "read
cluster `index` of this sample into `dest`" (the existing `AudioFileManager::loadCluster` path: sector reads
+ the 24-bit `convertDataIfNecessary`). **Per-asset backing kind** (`BACKING_HEAP` vs `BACKING_SLAB`, in the
C ABI now): cluster assets back onto the unmanaged slab (Step 7) — `materialize` fills a slab slot, eviction
frees via `deluge_slab_release`; variable assets (wavetable bands) use the TLSF heap. The manager records the
kind per chunk and routes alloc/free accordingly — the same "slab-release-else-`deluge_free`" the GMA's
`freeSdram` already does, subsumed here. (P0 stubs the routing to heap-only; P1 wires the slab calls.)

## Prefetch & async — the task system / embassy (decided 2026-06-21)

The manager is **policy + residency only**: no I/O, no scheduling. Prefetch is a separate *driver* on the
task system that, per active stream, `acquire`s cluster N+1 ahead of the playhead (read-ahead, takes a lease)
and `release`s cluster N−2 (release-behind). The RT render then finds cluster N resident+leased → no I/O, no
alloc on the audio thread. That's the RT-contract payoff; the manager already supports it (leases + cache-hit).

Sync vs async `materialize`, in two stages:

- **Now (matches firmware):** `materialize` is synchronous and *cooperatively yields* during the read (the
  `libdeluge/storage_wait.h` hook / `AudioEngine::runRoutine`, exactly as `loadCluster` does). The prefetch
  driver is a task on the existing cooperative scheduler. No async rework.
- **Later (embassy-native):** split blocking `acquire` into a **request / ready** pair so I/O can be truly
  async — a C-ABI addition, not a model change (the `Loading` lifecycle state already exists for it):
  - `deluge_resource_request(asset, index, size)` — reserve a slot, set the chunk `Loading`, return now;
  - an embassy storage task `.await`s the DMA read, then `deluge_resource_mark_ready(handle)`;
  - `deluge_resource_try_acquire(asset, index)` — returns the ptr only if resident+ready, else null (the RT
    path uses this; never blocks).

  The manager stays sync / `core::`; embassy owns the executor and the await. **The manager is the cache; the
  task system is the pump.**

## Implementation notes (P0, 2026-06-21)

- **Safe core, thin `unsafe` shell.** The asset/chunk tables are `&'static [Cell<Slot>]` of `Copy` PODs,
  indexed; all bookkeeping (leases, value function, eviction) is safe Rust. `Cell` gives reentrancy-tolerant
  interior mutability (no borrow to invalidate), so the reclaim hook re-entering during an allocation is
  sound with no `&mut self` anywhere. The manager **never dereferences a chunk's backing** (only stores it /
  passes it to `materialize`/free), so `unsafe` is confined to: FFI entry points, the one-time table setup
  over heap bytes, the `materialize`/`on_evict` fn-pointer calls, and the alloc/free calls.
- **miri:** validates the unsafe shell. (Run with a 16-aligned arena; a `deluge_alloc` TLSF end-sentinel
  `&mut BlockHeader` is flagged by miri as out-of-bounds — a pre-existing allocator provenance issue to fix
  separately, technically UB under LTO.)

## Key current-system references (what's being replaced)

- `src/deluge/memory/{cache_manager.{h,cpp},stealable.h,general_memory_allocator.{h,cpp}}`
- `src/deluge/storage/cluster/cluster.{h,cpp}` (`getAppropriateQueue`, `steal`, `mayBeStolen`, types)
- `src/deluge/storage/audio/audio_file_manager.cpp` (path map, mount, cluster size, reload, `cardReinserted`)
- `src/deluge/model/sample/sample_cache.cpp` (derived-data cache; `prioritizeNotStealingCluster`)
- `src/deluge/storage/wave_table/wave_table.cpp` (`WaveTableBandData`, band→table cascade)
- `src/deluge/dsp/granular/GranularProcessor.{h,cpp}` (`GrainBuffer` — scratch, should not be a cache asset)
