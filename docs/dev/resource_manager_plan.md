# Resource manager — execution plan & next steps

Companion to `resource_manager.md` (the design + the 4 resolved decisions). This is the
*ordered work plan* from where we are now. Status as of 2026-06-22.

## Where we are

- **Built & verified (committed):** the Rust resource manager — P0 (crate `deluge_resource` +
  C ABI `crates/deluge_resource/include/deluge_resource.h` + unit/proptest/conformance tests),
  P1.1 (per-asset slab backing: `BACKING_SLAB` → unmanaged cluster slab), the **safe-core
  refactor** (index + `Cell` tables, `unsafe` only at the FFI/alloc edges), and the **TLSF miri
  UB fix** (`BlockHeader` shrunk to 16 bytes). Miri clean across both crates.
- **P1.2a (coexistence wiring) — NOT in the current tree.** The branch was rebased/merged
  (`feat/resource-manager` ← `next`/embassy-scheduler/fiber/workers); `resourceManager_` is gone
  from `general_memory_allocator.{h,cpp}`. **Re-apply 2a** (below) as the first integration step.
- **A/B harness (user is setting up):** the Cordae project at `~/Deluge Backup`
  (`SONGS/problem_songs/Cordae/Cordae.XML`). Samples are *collected next to the song*
  (`.../Cordae/Cordae/*.wav`), so a **trimmed project** = the song + that sample subfolder, no
  `SYNTHS/`. A normal render yields ~56 s of audio.

## BLOCKER 0 — render-hang regression (must fix before A/B is meaningful)

`deluge_render` on the trimmed Cordae project **hangs**: 99.8% CPU, **0 stems**, ~9 MB RSS
(samples barely load) where it should produce ~56 s and exit in ~2 min wall. Song *load*
completes (`loading` → `exporting`); the spin is in the **export/playback cluster path**.

This is almost certainly in the committed allocator/slab cluster path (Step 7 slab, or the
cluster-load/eviction churn) — exposed by a sample-heavy looping session. It is a real
cluster-streaming bug, independent of (and prior to) the manager integration.

- **Diagnose:** ptrace *attach* is blocked (yama `ptrace_scope`); instead **launch** under gdb
  (parent can ptrace its child) and interrupt, or add a frame-count cap + per-cluster logging to
  the export loop. Bisect by temporarily reverting the cluster slab (`Cluster::create`/`destroy`
  back to plain `deluge_alloc`/`deluge_free`) to confirm the slab is the culprit.
- **Suspects:** slab acquire/release under heavy churn; a `deluge_alloc` reclaim retry loop;
  `DELUGE_DETERMINISTIC_ALLOC` memset cost on 32 KB clusters reloaded repeatedly (200× slowdown
  would be too much for memset alone → lean toward a loop); a cluster-load retry that never
  resolves (low RSS ⇒ loads failing/looping rather than data actually streaming).

### Findings 2026-06-22 (investigation pass)

Two *separate* faults were stacked; the first was masking the second.

1. **Null-deref crash (FIXED).** `ArrangerView::graphicsRoutine` (arranger_view.cpp:3186)
   dereferenced `outputsOnScreen[yDisplay]` without the null guard every sibling line has
   (3176/3180). During TRACK export the arranger scrolls/repopulates and leaves empty screen
   rows → null `output` → SIGSEGV at ~2:30 in. **Present on `next` too** (file is identical;
   confirmed both crash at this exact line under gdb), so it's a pre-existing latent bug, not a
   resource-manager regression. Fix: `if (output && output->recordingInArrangement)`.

2. **Eviction hard-hang at the SDRAM ceiling (Blocker 0 proper, OPEN).** With the crash fixed,
   the export streams clusters and RSS climbs ~linearly (≈6–8 MB/min) until the 64 MB host SDRAM
   is *full*, then **flatlines and spins forever — 0 of 8 stems** (RSS perfectly flat ⇒ stuck in
   a tight loop, not allocating). The reclaim plumbing looks correct in isolation
   (`deluge_alloc` retry loop drives `gmaSdramReclaim` → `cacheManager.reclaimOne`; the unmanaged
   slab registers no competing hook). Reproduces on the **slab build, a plain-`allocStealable`
   revert, and crash-fixed `next`** — i.e. it is in the shared stealable-queue/eviction path, not
   the slab specifically. **Smoking gun:** walking `cacheManager.reclamation_queue_` mid-render
   shows queue 6 (`NO_SONG_SAMPLE_DATA*`) with **~6960 nodes while only ~900 clusters can fit in
   the resident RSS** — the stealable linked-list is **corrupt** (cycle/duplicates or nodes
   pointing into reused memory). A corrupt queue makes `reclaimOne` refuse every candidate
   (`mayBeStolen` on garbage) → 512 refusals → `bypassCulling` → load returns null → spin.
   Next step: find who scrambles the `BidirectionalLinkedList` nodes — prime suspects are the new
   allocator handing back a slot still linked in a queue, or `DELUGE_DETERMINISTIC_ALLOC` zeroing
   a node that's still a live list member. `addReason()` *does* unlink on 0→1, so the double-add
   isn't there; look at `steal`/`~Stealable`/`freeSdram` ordering and slab slot reuse.

Repro project (samples are collected next to the song with `/`→`_` mangled names): reconstruct a
normal tree (`SONGS/Cordae.XML` + `SAMPLES/<path>` from `Cordae/<mangled>.wav`), then
`deluge_render --project <tree> --song SONGS/Cordae.XML --mode TRACK --out <dir>`. Diagnose by
**launching** under gdb (`ptrace_scope=1` blocks attach) and interrupting; the GMA singleton is
`_ZZN22GeneralMemoryAllocator3getEvE22generalMemoryAllocator` (`get()` is inlined).

## BLOCKER 1 (secondary) — SYNTHS-culling crash on the full backup

`deluge_render` on the *full* `~/Deluge Backup` SIGSEGVs in `Browser::sortFileItems`
(`strcmpspecial`, `second=0x0`) during `setupBlankSong` scanning the large `SYNTHS/` tree
(`cullSomeFileItems`). A `FileItem` name is null. **Likely a latent use-after-free** of a
FileItem name, *surfaced* by the sim's `DELUGE_DETERMINISTIC_ALLOC` zeroing freed memory
(garbage→clean-null→deref). May affect hardware. Investigate separately; the trimmed project
(no `SYNTHS/`) sidesteps it for A/B.

## A/B verification protocol (the gate for 2b)

- **A** = current/CacheManager path; **B** = raw clusters routed through the manager (2b).
- Render each: `deluge_render --project <trimmed> --song SONGS/problem_songs/Cordae/Cordae.XML
  --out <dir>` (deterministic by default), then **byte-diff the stem WAVs**.
- **Bit-exact ⇒ correct + pointer-independent.** A diff ⇒ investigate (re-baseline; the
  determinism harness should make it reproducible run-to-run).
- Keep it bounded (~56 s); the render must terminate (see Blocker 0).

## P1.2a — re-apply coexistence wiring (first integration step)

One heap reclaim hook must drive *both* coordinators during migration.

- Rust (already in the crate; re-confirm present): `deluge_resource_create_unhooked` (no
  auto-registered hook) + `deluge_resource_try_evict(mgr) -> bool`.
- C++ (`general_memory_allocator.{h,cpp}`): add `DelugeResource* resourceManager_`; create it
  *unhooked* lazily alongside `clusterSlab_` (`deluge_resource_create_unhooked(sdram_heap,
  asset_cap=512, chunk_cap=slab capacity)` + `deluge_resource_set_slab`). `gmaSdramReclaim`
  tries `deluge_resource_try_evict` **then** `cacheManager.reclaimOne`. Empty manager ⇒
  behaviour-identical (verify: sim boots + render-smoke unchanged).
- Build: add `crates/deluge_resource/include` to the firmware (`src/deluge/CMakeLists.txt`) and
  sim (`include_directories` + `HOST_INCLUDE_DIRS`) include paths.

## P1.2 — route raw SAMPLE clusters through the manager (REVISED 2026-06-23)

**The naive "materialize = `loadCluster`" model is infeasible.** `loadCluster` is deeply
coupled: it asserts/mutates `numReasonsToBeLoaded`, internally `addReason()`s + `removeReason`s,
gates on card state (`currentlyAccessingCard` / `clusterBeingLoaded` / `audioRoutineLocked`),
feeds the async `loadingQueue`, and is ~250 lines of intricate **inter-cluster boundary
byte-conversion** (it fixes up the *previous* and *next* clusters' boundary bytes). It cannot be
a clean materialize callback. So the long-term path **decouples I/O from residency**, in order;
each step is behaviour-preserving and **verified bit-exact against the Cordae golden**
(`scripts/golden_mixdown.sh check`).

Only `Cluster::Type::SAMPLE` moves to the manager; `SAMPLE_CACHE` / `PERC_CACHE_*` stay on
`CacheManager` (coexistence via the manager-then-CacheManager reclaim hook).

### Step 1 — extract a clean cluster reader ("ClusterSource")  ✅ DONE (2026-06-23, golden bit-exact)
Split `loadCluster` (audio_file_manager.cpp): the **data work** — `numSectors`, `disk_read`,
`convertDataIfNecessary`, and the prev/next inter-cluster boundary fixups — is now
`AudioFileManager::readClusterData(Cluster&, minNumReasonsAfter) -> bool`; `loadCluster` is thin
**orchestration** over it (guards, `clusterBeingLoaded`, the loading `addReason`/`removeReason`,
and the success/getOutEarly cleanup unified as `removeReasonFromCluster(ok ? "E034" : "E033")`).
The 250-line body was re-bracketed, not retyped (the `getOutEarly: return false` label sits above
the local inits so the backward gotos don't cross them). **Verified:** golden MIXDOWN bit-exact
(`a3b48a0a`), sim + `dbt build Debug` link, clang-format clean. This is the seam Step 2's
materialize Source uses.

### Step 2 — manager owns raw-cluster residency

**Step 2.0 — asset-lifecycle ABI ✅ DONE (2026-06-23).** Added
`deluge_resource_release_asset(mgr, asset)` (evicts the asset's resident chunks via `on_evict`,
then frees the slot) so the fixed-capacity asset table can't exhaust as Samples come and go.
Rust-tested; no C++ caller yet → golden bit-exact.

**Step 2.1a — stand up the Sample→Asset lifecycle ✅ DONE (2026-06-23, golden bit-exact).**
A `Sample` lazily defines its Asset (`Sample::ensureResourceAsset`): `owner=Sample*`,
`materialize` = placement-new `Cluster` + the Step-1 reader, `on_evict` = drop `clusters[i].cluster`
+ `~Cluster`, `cost=COST_IO`, `backing=BACKING_SLAB`; retired in `~Sample` via
`deluge_resource_release_asset`. GMA gained `resourceManager()` + an extracted
`ensureClusterSystem()`. `getCluster` defines the Asset on first stream but does **not** yet route
through `acquire` — defining an unacquired Asset is a no-op, so behaviour-neutral. Proved the
define-on-stream / release-on-destroy lifecycle in the live render (no asset-table overflow).

**Step 2.1b — flip residency ✅ DONE (2026-06-23, A/B ear-confirmed improvement).** Route
`SampleCluster::getCluster` through `deluge_resource_acquire` (miss → materialize, hit → lease);
`removeReasonFromCluster` → lease drop; `Cluster::addReason` → lease bump (new
`deluge_resource_add_lease`, hit-only). Manager-owned SAMPLE clusters severed from the CacheManager
queue; `numReasonsToBeLoaded` is a mirror. Per-Sample latch: `CLUSTER_DONT_LOAD` (recording) and any
read that can't get an Asset keep the Sample fully legacy (recorder stays legacy end-to-end). ENQUEUE
collapses to synchronous acquire (prefetch returns in Step 3).

**KEY FINDING — the bit-exact golden was invalidated by this flip (and that's good).** The headless
`deluge_render` bypasses the scheduler that pumps the async `loadingQueue`, so the **legacy** path
*starves*: streamed voices drop out (Cordae vocal stem silent 2–8s + ~17% partial dropouts). The
manager loads synchronously in `acquire`, so it renders the **complete** mix — confirmed correct by
ear (A/B: legacy was missing the vocals, manager is right). So legacy-in-the-headless-sim was never a
valid reference for streaming behaviour; the golden was **re-baselined to the manager render**
(determinism re-verified). Lesson: bit-exact A/B only gates the *pure refactors* (Steps 1, 2.0, 2.1a);
behaviour-changing steps need an ear-check / hardware, not byte-equality. True gate stays
**NEEDS-HARDWARE**.

<details><summary>Original 2.1b design notes (superseded by the above)</summary>

Route `SampleCluster::getCluster` through
`deluge_resource_acquire` (not-resident → materialize; resident → lease cache-hit), make
`removeReasonFromCluster` an `unlease`, and **sever** manager-owned SAMPLE clusters from the
CacheManager stealable queue (the sever is localized: `removeReasonFromCluster` must skip
`putStealableInAppropriateQueue` for a manager-owned cluster — the manager keeps it resident +
evictable instead; `addReason`'s `remove()` is harmless when not queued). `numReasonsToBeLoaded`
stays a **lease mirror**. Two evictors on one cluster = the 6960-node corruption, so a given
cluster must belong to exactly one system.

**Hazards found mapping the callers (these scope 2.1b):**
- **Recorder (hard special case).** `sample_recorder.cpp` calls `getCluster` with `CLUSTER_DONT_LOAD`
  (writing) *and* `CLUSTER_LOAD_IMMEDIATELY` (reading back) on the **same** Sample, and holds
  `numReasonsHeldBySampleRecorder`. A recording cluster is **written incrementally, not
  reconstructable from the card** — `readClusterData` (the materialize Source) would read garbage.
  Recording clusters must stay legacy (same shape as the P3 caches). ⇒ A Sample that is being
  recorded must NOT be manager-owned.
- **`CLUSTER_DONT_LOAD`** (recorder + `audio_clip.cpp:232`) = allocate-without-loading; the manager's
  `acquire` always materializes. ⇒ DONT_LOAD stays on legacy `Cluster::create`.
- **Async `loadingQueue` (`CLUSTER_ENQUEUE`)** vs the manager's **synchronous** `acquire`. Step 3
  relocates prefetch via the manager's own `request`/`mark_ready`. For 2.1b, ENQUEUE on a
  manager-owned Sample collapses to a synchronous `acquire` (load-on-demand). Golden stays bit-exact
  (bytes are identical regardless of *when*/*whether* a cluster is resident — any evicted cluster
  reloads identical bytes), but on-hardware audio-thread loads become synchronous **until Step 3**.
- **Per-Sample coexistence boundary.** Cleanest: a Sample is manager-owned iff it has a valid Asset
  *and* is a pure-read stream (not recording, no DONT_LOAD); otherwise it stays fully legacy
  (`acquire` returns the cluster, legacy keeps `Cluster::create` + the stealable queue). The
  read-vs-record split is per-Sample, not per-cluster, to avoid one Sample's clusters spanning both
  evictors.

</details>

### Step 3 — relocate prefetch ✅ DONE (2026-06-23, golden bit-exact 5174c4e6)
Async prefetch restored for the manager path, so the audio thread never blocks on SD (2.1b's
synchronous `acquire` was fine offline but a hardware regression). Done by a **construct/load split**
rather than a brand-new pending-queue/embassy ABI:
- Rust foundation: a `construct` Source callback (init the chunk object, no I/O) + `deluge_resource_request`
  (reserve + construct + lease, no materialize) + `deluge_resource_set_construct`. `acquire` stays the
  synchronous full path (LOAD_IMMEDIATELY). Unit-tested.
- C++: `getCluster(CLUSTER_ENQUEUE)` → `request` + enqueue on the **existing** `loadingQueue` (returns the
  cluster `loaded==false`); the loader fills it via `readClusterData` off the audio thread.
  `LOAD_IMMEDIATELY` → `acquire` (+ force-read a prefetch-constructed hit). `loadAnyEnqueuedClusters` reads
  manager clusters via `readClusterData` directly (NOT `loadCluster` — its reasons would desync the lease,
  its `audioRoutineLocked` guard refuses to load). `clusterConstruct` = async init; `clusterEvict` erases
  from `loadingQueue`.
- **Real bug fixed:** `StemExport::renderWait` (the offline render loop) never pumped the loader —
  `loadAnyEnqueuedClusters` bails while `audioRoutineLocked`, which the offline `AudioEngine::routine()`
  holds for its whole body, so the in-routine pump (line 1081) is a no-op. (This *was* legacy's dropout
  root cause; 2.1b only worked by loading synchronously.) Now pumped between routines (lock clear), as the
  on-device scheduler does. With it, async streaming renders the complete mix **bit-exact with the golden**.

The manager's own `mark_ready`/`try_acquire` + `Loading`-state ABI (fully replacing the C++ `loadingQueue`)
still lands with the embassy/RT-prefetcher move; this step reuses the existing queue, so only the pump task
changes then.

### Step 4 — retire
`Stealable` / `CacheManager` / `numReasonsToBeLoaded` / `getAppropriateQueue`; the GMA
coordinator dissolves.

## After P1.2

- **P2 — soft references from the project** (`deluge_resource_reference`): the song marks the
  assets it uses, so eviction prefers current-song clusters last. Shim until `project_model`
  exists.
- **P3 — the derived caches** (`SAMPLE_CACHE`, `PERC_CACHE`, `WaveTableBandData`): these are
  *written incrementally during playback*, not reconstructed on demand — they don't fit
  `materialize`. Needs a design pass (a recompute Source, or a dirty-resident model). Until then
  they stay on CacheManager (coexistence).
- **P4 — retire** `Stealable` / `CacheManager` / `numReasonsToBeLoaded` / `getAppropriateQueue`;
  the GMA coordinator dissolves (its remaining callers move to the resource ABI). Gate: P3 done
  + hardware-verified.

## Deferred / parallel

- **Async request/ready ABI** (`deluge_resource_request` / `mark_ready` / `try_acquire` +
  `Loading` state) for embassy — lands with the RT prefetcher; the model already accommodates it.
- The Cordae render-cap idea is unnecessary once Blocker 0 is fixed (the render terminates).

## Key paths

- Rust: `crates/deluge_resource/src/{manager,value}.rs`, `crates/deluge_alloc/src/{tlsf,slab}.rs`,
  `crates/deluge_resource/include/deluge_resource.h`.
- C++ integration: `src/deluge/memory/general_memory_allocator.{h,cpp}` (coexistence hook),
  `src/deluge/storage/cluster/cluster.{h,cpp}`, `src/deluge/model/sample/{sample_cluster,sample_cache}.cpp`,
  `src/deluge/storage/audio/audio_file_manager.{h,cpp}`.
- A/B: `src/bsp/host/host_render_main.cpp`; trimmed project recipe = song +
  `SONGS/problem_songs/Cordae/Cordae/` samples, no `SYNTHS/`.
