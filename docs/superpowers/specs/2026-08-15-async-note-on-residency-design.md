# Async note-on residency: warm hints + defer-not-drop

**Status:** design approved 2026-08-15, not yet implemented.
**Supersedes in part:** the deferred half of
`2026-07-24-deadline-elimination-research.md` (that research's "above-the-port,
low-urgency, schedule on measured evidence" item — the evidence now exists).

**Goal:** a sample note-on must never fail because its first cluster has not
loaded *yet*, and nothing may block to achieve that.

## 1. The problem, as measured

Reproduced on hardware 2026-08-15 (GCC `-Og` release image, verified image
identity — `fault COMMIT: 6141a126f` matched the flashed ELF):

```
525.5489: assignClusters fail: acquire LOADING cluster 0 dir 1 prio 1
525.5489: setupClustersForPlayFromByte fail / byte: 1024
525.5489: setupClustersForInitialPlay fail
525.5535: assignClusters fail: acquire LOADING cluster 0 dir 1 prio 1   <- retry 4.6 ms later
525.5535: setupClustersForInitialPlay fail
525.5536: fault PTR: 0x201d3a05 ... (E199)
```

`LOADING`, not `UNAVAILABLE`: the geometry is valid, the reservation succeeded,
and cluster 0 *was* enqueued. It simply had not finished loading — twice, 4.6 ms
apart.

### Root cause

`ManagerResidency::acquire` (`crates/deluge_sample_source/src/manager_residency.rs:105`)
has no blocking path. On a miss it `request`s a reservation, `loader_enqueue`s
it, calls `deluge_streaming_signal_fill()`, and returns `Loading`.

`SampleLowLevelReader::assignClusters` calls the **boolean**
`deluge_sample_region_acquire`, true only for `Ready`. So it *schedules a fill
and immediately abandons it*. `setupClusersForInitialPlay` has no defer or
retry, unlike `attemptLateSampleStart` (`voice_sample.cpp:215-262`), which
handles `Loading` by deferring to a single wait site.

**The initial-play path treats "not loaded YET" as "will never load."**

Legacy `Cluster::getCluster()` loaded synchronously, so first play always had
its cluster in hand. The region port made acquire non-blocking and nothing
replaced the implicit load. Confirmed by `git log -S "claimClusterReasons"`:
the preview path never warmed its start cluster, so this is not a deleted call
but a capability that used to be free.

### Why only the preview shows it

Verified on the same hardware run: ordinary sample playback is fine. Only the
preview starts a note close enough to its own file-open for the fill not to have
landed. Every other note-on plays a sample whose cluster 0 went resident long
before. The defect is general; the preview is merely the path that exposes it.

## 2. Rejected approach: block on the fiber

The legacy-faithful fix is to block until loaded, reusing
`deluge_streaming_fill_chunk_blocking`'s on-fiber `block_on_fiber` path
(`src/bsp/rust/src/streaming_loader.rs:174`). **Rejected**, for two independent
reasons:

1. Per `2026-08-08-storage-execution-model-end-state-design.md`, a wait on a
   storage-tier task is **progress-class** — legal only once
   `streaming_fill_task` runs on `STREAM_EXEC` (R5a Phase 1). It is not legal
   today.
2. It is the wrong direction: the architecture is moving toward fewer suspended
   stacks, not more.

## 3. Architecture

Three layers. Two are in scope.

| Layer | Purpose | Scope |
|---|---|---|
| 1. Warm hint | make misses **rare** | in scope |
| 2. Defer-not-drop | make the residual miss **graceful** | in scope |
| 3. `STREAM_EXEC` promotion | make deferral **guaranteed** to converge | dependency (R5a Phase 1) |

Layers 1 and 2 are independent and separately valuable: Layer 1 without Layer 2
leaves rare misses fatal; Layer 2 without Layer 1 makes every preview start
late. Ship both.

## 4a. CORRECTION 2026-08-16 — Layer 1's premise was wrong

§4 below was written believing the preview never warmed its start region. **It does**, and §4 as
written would have been a no-op. Measured on hardware over five instrumented flashes:

```
21.8760: reserve: asset 2 covered 2 leased 2 mode 1 headByte 1024 dir 1 cl0 0 cl1 1 audioStart 1024
23.1153: assignClusters fail: acquire LOADING cluster 0 dir 1 asset 6 streamAsset 6 resident 1 leases 1
```

`previewSample` calls `loadFile(..., CLUSTER_LOAD_IMMEDIATELY)` → `claimClusterReasons` →
`loadModeFor` → `DELUGE_LOAD_NOW` → `deluge_sample_reserve_open`. Mode 1 is `NOW`, and
`leased == covered` under `NOW` means `fill_now` returned true and `mark_ready` ran. Cluster 0 (`cl0
0`) really is materialized, ~11 ms before the note.

Ruled out by measurement, in order: a failed synchronous fill (`leased == covered` every time); an
asset-identity mismatch (`asset N == streamAsset N`); a wrong anchor cluster (`cl0 0` is the cluster
requested); a stale reservation across a sample swap (`AudioFileHolder::setAudioFile` closes it,
`audio_file_holder.cpp:64`); and duplicate chunks (`Manager::request` dedupes via `find_resident`,
`manager.rs:859-871`).

**What remains — a residency LIFETIME defect.** At the failure the manager reports `resident 1
leases 1`. `peek` differs from `try_acquire` only by the `ready` gate, so the chunk is
resident-but-not-ready; and the lease count is 1, not the 2 it would be if the reservation's lease
and the cursor's new lease were both on one chunk. So the chunk the cursor holds is a FRESH one:
the warmed chunk lost its last lease, became evictable, was evicted, and `request` allocated an
unready replacement under the same key — `ready = false, leases = 1`, reported as `LOADING`.

The warm hint therefore works and is then dropped before the note consumes it. Previews are
dispatched as worker-fiber ops (`sample_browser.cpp:590`, `Owner::run`) and each `previewSample`
begins with `stopAnyPreviewing()`, so rapid scrolling — which is how this reproduces — lets a later
preview release the reservation that an earlier, still-pending note-on was relying on.

**Layer 1 is therefore a lifetime fix, not a new warm hint:** the start region must stay pinned
until the note it was warmed for has actually started. The next investigation step is to establish
exactly which release fires between materialization and note-on — the reservation close in
`stopAnyPreviewing`'s `setAudioFile(nullptr)`, or a `reanchor` — before choosing where the pin is
extended.

**Layer 2 is unaffected and is now clearly the primary fix.** Whatever drops the residency, a
note-on that responds to `LOADING` by deferring converges as soon as the refill lands, instead of
dropping the voice and asserting E199.

## 4b. ROOT CAUSE PROVEN 2026-08-16 — a reservation re-anchored across an asset change

```
23.8195: reserve: res 20038b80 resAsset 1 asset 2 covered 2 leased 2 mode 1 headByte 1024 cl0 0 cl1 1
23.8300: assignClusters fail: acquire LOADING cluster 0 dir 1 asset 2 streamAsset 2 resident 1 leases 1
```

`resAsset` is the Asset the reservation is BOUND to (read from Rust); `asset` is what C++ just
computed for the sample now in the holder. **They diverge: 1 against 2.**

### The mechanism

1. `Reservation` captures `asset` in `open()` (`reservation.rs:170-195`). **`reanchor` never changes
   it** — by design, it slides the window *within* one Asset.
2. `claimClusterReasonsForMarker` (`sample_holder.cpp:202-207`) chooses open-vs-move on
   `reservation == nullptr` **alone** — never on whether the underlying sample changed.
3. A `SampleHolder` reused for a different sample without an intervening close therefore takes the
   `move` branch, and re-anchors the OLD Asset's window onto the NEW sample's marker.

So the reservation dutifully pins and materializes clusters 0 and 1 **of asset 1** (hence
`covered 2 leased 2`, which is why the counters looked healthy all along), while the note-on needs
cluster 0 **of asset 2** — which nothing ever warmed. The cursor then requests it cold:
`resident 1, leases 1, ready = false` → `LOADING`.

The preview reproduces it because rapid scrolling reuses one holder across samples faster than the
close path runs: the trace shows a `CLOSE start` before the second preview but **none** before the
third, which is the one that fails.

### The fix

The invariant "a reservation is bound to one Asset for its whole life" is real and worth keeping —
what is missing is enforcement at the seam that can violate it. Two options:

1. **Rust (preferred):** give `deluge_sample_reserve_move` the `source_id`. If it differs from the
   bound Asset, release every lease and rebuild against the new one (a close+open that preserves the
   handle). No caller can then get this wrong, and `time_stretcher.cpp:1140`'s reservation gets the
   same protection for free. Costs an ABI parameter change on `move`.
2. **C++ only:** in `claimClusterReasonsForMarker`, close and reopen when
   `deluge_sample_reserve_asset(reservation) != sourceId`. Smaller, but leaves the invariant
   enforced by convention at every call site rather than by the type.

Either way `deluge_sample_reserve_asset` — added for this investigation — stays, since it is what
makes the invariant checkable.

This is a genuine bug independent of the preview: ANY holder reused across samples mis-pins. It also
explains the shape of the E199 exactly, and Layer 2 remains the right belt-and-braces fix, since a
cold cluster 0 should defer the note rather than kill the voice.

## 4. Layer 1 — warm the preview's start region

The warm-hint mechanism **already exists below the port** and needs no new
C-ABI surface. `crates/deluge_sample_reader/src/reservation.rs` implements
`Reservation`, exposed as:

```c
DelugeSampleReservation* deluge_sample_reserve_open(uint32_t source_id, uint64_t marker_frame,
                                                   int8_t direction, DelugeLoadMode load_mode);
void deluge_sample_reserve_move(DelugeSampleReservation* res, uint64_t marker_frame,
                                int8_t direction, DelugeLoadMode load_mode);
void deluge_sample_reserve_close(DelugeSampleReservation* res);
```

It pins a small forward/backward window of cluster residency, holds one real
lease per covered cluster, and Rust owns pin, budget, eviction and fill.
`DELUGE_LOAD_ENQUEUE` never blocks. Live callers today: `time_stretcher.cpp:1140`
and `sample_holder.cpp:201` (via `claimClusterReasons`).

**Change:** the sample-preview path opens one reservation over its start marker
with `DELUGE_LOAD_ENQUEUE` when the preview file is resolved, and closes it in
`AudioEngine::stopAnyPreviewing()`. Exactly one reservation is open at a time —
`stopAnyPreviewing` already runs before each new preview.

Browse-to-play latency is hundreds of milliseconds; a cluster read is ~1 ms. The
fill has three orders of magnitude more time than it needs.

**Not built:** a new `deluge_sample_source_warm` entry point. The existing
reservation API *is* the SR2 warm hint in the shape the July research asked for
(declaration above, policy below). Adding a parallel surface would duplicate it.

## 5. Layer 2 — defer-not-drop

### 5.1 Propagate the tri-state

Three signatures stop collapsing the port's tri-state into `bool`. Returning the
existing `DelugeRegionState` (from `include/libdeluge/sample_source.h`) avoids
inventing a parallel enum:

```cpp
// src/deluge/model/sample/sample_low_level_reader.h
DelugeRegionState assignClusters(SamplePlaybackGuide*, Sample*, int32_t clusterIndex,
                                int32_t priorityRating);                                  // was bool, private
DelugeRegionState setupClustersForPlayFromByte(SamplePlaybackGuide*, Sample*,
                                               int32_t startPlaybackAtByte, int32_t priorityRating);
DelugeRegionState setupClusersForInitialPlay(SamplePlaybackGuide*, Sample*, int32_t byteOvershoot = 0,
                                             bool justLooped = false, int32_t priorityRating = 1);
```

`DELUGE_REGION_READY` replaces `true`; `UNAVAILABLE` replaces `false`; `LOADING`
is the new third outcome. Every existing call site keeps today's behaviour by
testing `!= DELUGE_REGION_READY` — that is a mechanical, behaviour-preserving
edit, and only the sites listed in §5.3 gain new behaviour.

`reassessReassessmentLocation` stays `bool`: it is a mid-playback reassessment,
never a note start, and has no deferral path to take.

### 5.2 Convert LOADING into deferral

In `VoiceUnisonPartSource::noteOn` (`voice_unison_part_source.cpp:59`):

```cpp
DelugeRegionState state = voiceSample->setupClusersForInitialPlay(guide, sample, 0, false, 1);
switch (state) {
case DELUGE_REGION_READY:
    return true;                                    // sounds now — unchanged
case DELUGE_REGION_LOADING:
    // The fill is in flight. Admit the voice and let the existing late-start
    // machinery start it as soon as the chunk lands: a non-zero
    // pendingSamplesLate makes voice.cpp's tryToStartMidNote fire next render.
    voiceSample->pendingSamplesLate = 1;
    return true;
case DELUGE_REGION_UNAVAILABLE:
    return false;                                   // genuinely unplayable — unchanged
}
```

Nothing else is needed, because the deferral machinery already exists:
`voice.cpp:2065` tests `voiceSample->pendingSamplesLate`, computes
`rawSamplesLate`, and calls `attemptLateSampleStart`, whose three outcomes are
already handled — `SUCCESS` starts the note, `WAIT` skips one render and retries,
`FAILURE` unassigns the voice.

`pendingSamplesLate = 1` means "one sample late", which is inaudible, and each
`WAIT` adds `numSamples` so the start position advances in real time — the note
sounds from where it would have been, not from where it was requested.

### 5.3 Scope of the new behaviour

- **In:** `VoiceUnisonPartSource::noteOn` — every sample voice.
- **Out:** `audio_clip.cpp:428`. `pendingSamplesLate` is explicitly unused for
  AudioClips (`voice_sample.h:87`), so AudioClip keeps today's behaviour and
  treats anything but `READY` as failure. Deferring there needs a different
  mechanism and is not in this spec.
- **Out:** `time_stretcher.cpp:1006` and `voice_sample.cpp:256` — mid-playback
  and already-deferring sites respectively; both keep `!= READY` semantics.

### 5.4 Why deferral terminates without a cap

No explicit timeout is added, and none is needed. `WAIT` accumulates
`pendingSamplesLate += numSamples`, so the late-start position advances in real
time. When it passes the sample's end, `acquire` returns `UNAVAILABLE`
(the `index >= num_clusters` guard in `ManagerResidency::acquire`) →
`attemptLateSampleStart` returns `FAILURE` → the voice unassigns. **The bound is
the sample's own length**, already enforced below the port.

Worst case, stated plainly: if a fill never lands, a voice occupies a slot,
silent, for as long as the sample is long. That is strictly better than today's
silent drop (the note is recoverable if the fill lands) but it is not free —
see §8.

## 6. The E199 assertion is separately wrong

`Sound::noteOn` calls `checkVoiceExists(voice, "E199")` on a voice that is no
longer in `voices_`, so E199 misreports whatever removed it.
`acquireVoice()` does `voices_.push_back` before `noteOn` (`sound.cpp:4855`) and
the reuse path keeps its voice in the list (`sound.cpp:1627-1631`), so **what
removes it is not yet established.**

Layer 2 makes the `LOADING` route to this assertion unreachable, but
`UNAVAILABLE` still reaches it. Fixing it requires finding the remover — not
softening the assertion. Kept in scope; the plan must investigate before
changing anything.

## 7. Testing

**Byte-identical `READY` path.** §5.1 is a mechanical signature change and must
not move a single rendered sample. Gates: `region-source-differential`,
`region-fill-differential`, `region-read-differential`, `golden-embassy-diff`
(all via `./dbt harness run <name>`).

**New behaviour, host, no hardware.** `deluge_sample_source`'s Scenario-driven
residency provider can hold a chunk in `Loading` indefinitely and then release
it, so a unit spec can assert: `LOADING` at note-on admits the voice; the voice
converges to sounding once the chunk lands; a chunk that never lands unassigns
the voice when the position passes the sample end (§5.4), proving termination.

**Hardware.** (a) the preview repro that currently yields E199 must play; (b) an
ear-check under polyphonic load, because §8.

## 8. Risks

**Held residency under pressure.** A deferred voice keeps its slot and its
leases while silent. Today's drop frees them. Under memory pressure this trades
a silent drop for held residency at exactly the moment things are tight, which
could raise eviction pressure and, in the worst case, make a marginal situation
worse rather than better. Culling still applies, so it is bounded, but this is
the one behaviour most likely to surprise on hardware and the reason for the
polyphonic ear-check.

**Deferral without Layer 3.** Until `streaming_fill_task` runs on `STREAM_EXEC`,
a deferred voice's convergence depends on the fill task getting scheduled. It
does today (the Embassy executor runs it), but it is not *guaranteed* under
audio-tier load. Layer 3 upgrades this from "works in practice" to "correct by
construction". This spec must not be read as making Layer 3 optional.

**`-Og` measurement caveat.** The hardware evidence above was gathered on a
`-Og` image whose cull metric runs well over budget (see `CMakeLists.txt`'s
`DELUGE_DEBUG_OPT_LEVEL` note). Timing-sensitive conclusions should be
re-checked on `relwithdebinfo` before being treated as shipping behaviour.

## 9. Out of scope

- Layer 3 / `STREAM_EXEC` promotion — R5a Phase 1, its own ladder.
- Migrating other `claimClusterReasons` callers to direct reservation use.
- Cost-aware voice admission and bounding the `TimeStretcher::hopEnd` spike —
  both named in the July research, both still evidence-gated.
- AudioClip deferral (§5.3).
