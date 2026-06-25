# Audio-path allocation audit (Phase 2 grounding)

Phase 2 of [`allocator_implementation_plan.md`](./allocator_implementation_plan.md) assumes "the only RT
allocations are cluster slots" and lists confirming that as an open question. This is that confirmation —
a trace of every allocator call reachable from the RT render path (`deluge_app_render()` in
`audio_engine.cpp:552`, which the BSP calls per audio block; voice rendering runs inside it). It is **not**
true that cluster slots are the only RT allocation: there are **three distinct RT-allocation families**, and
the restructure must address all three (or the `deluge_alloc_rt` guardrail will trip on the survivors).

## Family 1 — Cluster slot acquisition (the documented target)

`SampleCluster::getCluster(..., CLUSTER_ENQUEUE)` (`sample_cluster.cpp:66`) lazily calls
`Cluster::create()` (`cluster.cpp:40`) → `GeneralMemoryAllocator::allocStealable(sizeof(Cluster) + size)`
(`cluster.cpp:42`), which can trigger a synchronous steal on the render thread. The SD *read* is already
deferred (`loadingQueue.enqueueCluster`); the *slot alloc + steal* is not. RT call sites:

| Site | Context |
|---|---|
| `voice_sample.cpp:202` | first cluster(s) for a starting sample voice |
| `voice_sample.cpp:847` | advancing into the next uncached cluster |
| `sample_low_level_reader.cpp:305, 382` | low-level reader cluster fetch / advance |
| `time_stretcher.cpp:1126` | time-stretch reading ahead into the next cluster |

Plus a **second flavor** the plan's prose underweights — *cache-write* cluster creation:
`SampleCache::setupNewCluster()` (`sample_cache.cpp:141`) → `Cluster::create(Type::SAMPLE_CACHE, ...)`
(`sample_cache.cpp:154`), reached from `voice_sample.cpp:913` while *writing* the repitch/percussion cache
during render. Same `allocStealable` path; same uniform ~32 KB slot. The slab pool must serve both the
read-streaming and cache-write cluster traffic.

## Family 2 — TimeStretcher scratch buffer (non-cluster RT alloc)

`TimeStretcher::allocateBuffer()` (`time_stretcher.cpp:1046`) → `allocMaxSpeed(kBufferSize * 4 *
numChannels)`, with matching `delugeDealloc(buffer)` on teardown. Called from the render path at
`time_stretcher.cpp:1017` and `:1166` (and freed at `:175, :956, :1038, :1174`). This is exactly the
"any other RT allocator call must be hoisted off-thread or pre-reserved" case from the plan's open
questions — a variable-size SRAM allocation **and free** on the render thread, independent of the cluster
slab. Options: pre-reserve the buffer when the time-stretching voice is solicited (off-RT), or carry it as
a pooled fixed-size scratch tied to the voice's lifetime.

## Family 3 — ObjectPool miss (Voice / VoiceSample / TimeStretcher)

`ObjectPool::acquire()` (`object_pool.h:99`) allocates via its `fast_allocator` when the pool is empty
(`object_pool.h:101-103`). The pooled types are `Voice`, `VoiceSample`, `TimeStretcher`
(`audio_engine.h:139-141`), solicited during note-on (`AudioEngine::solicitVoiceSample()` etc.), which can
fire from the sequencer *inside* the audio routine. So an empty-pool note-on is an RT SRAM allocation. The
watermark/prefetch idea applies here too: keep the pools topped up off-RT (`repopulate()` already exists,
`object_pool.h:84`) and make an empty-pool `acquire` on the RT thread a graceful voice-cull (an underrun,
not an allocation), mirroring the cluster-miss fallback.

## Consequences for the Phase 2 plan

- The "slab pool of uniform 32 KB slots" covers **Family 1 only**. Families 2 and 3 need their own
  off-RT pre-reservation strategy; the single tunable "lookahead depth" becomes *three* watermarks
  (cluster slots, voice/sample/stretcher pools, stretch buffers).
- The **`deluge_alloc_rt` guardrail cannot be a hard trap until all three families are hoisted** — until
  then a debug build would FREEZE on the first streamed/time-stretched/voiced note. Sequencing: land it in
  a *counting/logging* mode first (instrument + prove the path), flip to hard-assert only once the render
  path is provably allocation-free.
- **Verification gap on this branch:** Phase 2 is behavioral (voice-culling timing shifts, underrun
  behavior) and the plan mandates golden re-baseline + ARM/QEMU validation + memory-pressure underrun
  stress. None of that exists on `next` (no goldens, no hardware-in-loop here). The audit and the guardrail
  *mechanism* are safe to land; the behavioral restructure (request/enqueue + slab + watermarks) should
  wait for a branch with the golden fixtures + hardware, or it ships blind.

## RT render thread identity (for the guardrail)

`deluge_app_render()` is the precise bracket — voice rendering happens inside it, and the off-RT loader
(`AudioFileManager::loadAnyEnqueuedClusters`, which is *allowed* to allocate) is called separately from
`routine()`/the SD routine, not from within `deluge_app_render()`. So a single flag set around the body of
`deluge_app_render()` cleanly distinguishes "RT render" (no alloc) from the off-RT loader (alloc OK).
`audioRoutineLocked` is **not** a suitable signal — it is also set by non-RT code (menus, sample browser,
playback setup) to *prevent re-entry*, so it does not uniquely mean "inside render."
