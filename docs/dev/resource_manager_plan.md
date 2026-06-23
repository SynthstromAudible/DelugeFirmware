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

## P1.2b — route raw SAMPLE clusters through the manager (coexist with caches)

Only `Cluster::Type::SAMPLE` (raw sample data) moves to the manager; `SAMPLE_CACHE` /
`PERC_CACHE_*` stay on `CacheManager`.

- **Asset per Sample:** define on Sample creation — `owner=Sample*`,
  `materialize` = read cluster *i* from the card (the `AudioFileManager::loadCluster` read +
  the placement-`new Cluster` + 24-bit conversion), `on_evict` = `~Cluster` + null
  `sample->clusters[i].cluster` (replaces `Cluster::steal`), `cost=COST_IO`,
  `backing=BACKING_SLAB`. Store the asset id on the Sample.
- **`SampleCluster::getCluster` (sample_cluster.cpp):** the not-resident branch becomes
  `manager.acquire(asset, clusterIndex, slot)` → backing (materialize loads); the
  already-resident branch's `addReason` becomes a lease (`acquire` cache-hit).
- **Leases:** `Cluster::addReason` / `AudioFileManager::removeReasonFromCluster` →
  `deluge_resource_acquire` (lease++) / `deluge_resource_release` (lease--). At 0 leases the
  chunk stays cached + evictable (replaces the CacheManager queue for raw clusters); the
  manager's eviction + `on_evict` replace `removeReasonFromCluster`'s queue/delete logic.
- **Async enqueue** (`CLUSTER_ENQUEUE` → `loadingQueue` → `loadAnyEnqueuedClusters`): the
  manager's `acquire`/`materialize` is synchronous. For P1 keep the immediate-load path through
  `acquire` and leave the async prefetch as-is, OR map enqueue onto the deferred
  request/ready ABI — **decide during impl** (the async request/ready surface is deferred; see
  resource_manager.md). The sim render drives loads synchronously, so A/B works either way.
- **`Cluster::size`** is finalized at boot (never raised — `cardReinserted`), so the slab slot +
  asset materialize size are fixed for the session.
- **Verify:** A/B bit-exact; sim render works; no eviction/reload corruption; `dbt build Debug`
  + ctest green. NEEDS-HARDWARE before merge.

## After 2b

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
