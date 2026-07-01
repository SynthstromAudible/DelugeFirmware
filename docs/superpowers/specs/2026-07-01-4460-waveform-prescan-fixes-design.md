# Design: Fix code-review findings for the #4460 waveform overview pre-scan

**Issue:** #4460 — "Rendering audio clip waveform as a single row in song row view UI gets laggy under load" (release-blocker).

**Context:** A working-tree change (7 files, +284/−2, uncommitted) adds two optimizations to zoomed-out single-row waveform rendering:

- **Fix A** — `findPeaksPerCol` reuses cached per-column peaks on horizontal scroll (slides `colStatus`/`minPerCol`/`maxPerCol` instead of full-invalidating) when scrolling by a whole number of columns.
- **Fix B** — a throttled background pre-scan (`AudioFileManager::backgroundWaveformOverviewScan` → `WaveformRenderer::advanceOverviewScan` → `investigateWholeCluster`) caches each cluster's coarse int8 min/max off the render path, so zoomed-out rendering gets cache hits and never loads clusters synchronously mid-scroll.

A high-effort code review surfaced correctness bugs, performance costs, and a duplication/altitude problem. This spec defines how to fix all of them.

## Goals

- Eliminate the wrong-rendering bugs the pre-scan introduces (and one it shares with pre-existing code).
- Keep the background scan from competing with playback audio or burning idle CPU forever.
- Remove the code duplication at the root of the most dangerous bug, so the render path and the pre-scan cannot drift apart.

## Non-goals

- No broader refactor of `findPeaksPerCol`'s cluster-selection logic (the `numClustersSpan` branches at lines 356-419 stay as-is).
- No change to the render/brightness math beyond what the fixes require.

## Root-cause insight

The inverted-sentinel bug (#1) is **shared** with the original `findPeaksPerCol`, not unique to the pre-scan. When audio ends exactly on a cluster boundary:

```
limit = (numValidBytes + audioDataStartPosBytes) & (Cluster::size - 1)   // == 0 on exact boundary
```

A *full* final cluster is then treated as zero-length, and both paths stamp the `SampleCluster` defaults (`minValue=127`, `maxValue=-128`). The reuse path at `waveform_renderer.cpp:443-445` later loads `min = 127<<24` (large +) and `max = -128<<24` (large −), i.e. `min > max`, drawing a garbage/bright column. The pre-scan makes this fire systematically because it proactively stamps every cluster.

Correct form (rounds up, mirroring `getFirstClusterIndexWithNoAudioData`):

```
limit = ((numValidBytes + audioDataStartPosBytes - 1) & (Cluster::size - 1)) + 1   // == Cluster::size on exact boundary
```

Fixing this once in shared code fixes both paths.

## Approach: extract two shared helpers

The whole-cluster investigation in `findPeaksPerCol` is really two reusable pieces tangled inside the per-column loop. Extract them; both the render path and the pre-scan call them.

1. **`scanClusterPeak(cluster, startByte, endByte, byteDepth, numChannels) → {min32, max32}`** — the inner read loop (subsampling by `SAMPLES_TO_READ_PER_COL_MAGNITUDE`, stereo odd-stride, `byteDepth − 4` misalignment). The pre-scan already has this as a free function; point `findPeaksPerCol`'s inner loop (lines 530-549) at it too.

2. **`storeWholeClusterPeak(sampleCluster, min32, max32)`** — fold in the previously-captured min/max, store the int8 pair with round-toward-zero, and set `investigatedWholeLength = true`. Critically, this is **only called when real data was scanned**; the empty-span early-out no longer stamps a bogus result. This structurally eliminates #1's inverted-sentinel case.

This removes the duplication (#6) and fixes #1 at the root with a small, verifiable change and no rewrite of the cluster-selection logic.

## Fixes

### Correctness

- **#1 Inverted sentinel** (`waveform_renderer.cpp` pre-scan early-outs + shared `limit`): apply the rounded-up `limit` in shared code; the empty-span path no longer marks a cluster investigated. Store a valid result only when data was read.
- **#2 int32 byte-offset overflow** (>2 GB files):
  - New pre-scan math (`clusterStartByteAbs = clusterIndex << Cluster::size_magnitude` and the derived alignment) done in `int64_t`/`uint64_t`.
  - **Also fix the pre-existing overflow** at `findPeaksPerCol:340-345` — `colStartByte`/`colEndByte` are `int32_t` and overflow past 2 GB; widen to 64-bit (and the `>> Cluster::size_magnitude` cluster-index derivation accordingly).
- **#3 Shrink invariant** (`waveform_renderer.cpp` Fix A reuse path): record the sample's valid length in `WaveformRenderData`. When zoom is unchanged but the length changed, the whole-column-shift reuse path falls back to full invalidate, so `INVESTIGATED` columns past a shrunk waveform end cannot survive and render stale data.
- **Shift-count narrowing** (lower severity, same area): guard the `int64 → int32` narrowing of the whole-column shift count before calling `shiftRenderCache`, so a pathologically large scroll delta full-invalidates instead of wrapping.

### Performance / design

- **#4 Blocking I/O in `slowRoutine`**: reduce `kOverviewScanClustersPerCall` to 1 and re-check the card/audio guards (`cardEjected`/`currentlyAccessingCard`/`clusterBeingLoaded`/`audioRoutineLocked`) between loads inside the scan, so a slow card can't chain multiple synchronous sector reads per tick against playback.
- **#5 Idle re-scan forever**: add a per-sample "fully scanned" short-circuit and a manager-level all-done flag so `backgroundWaveformOverviewScan` early-returns once every sample is cached. Re-armed by `resetOverviewScan` / `markAsUnloadable`.

### Cleanup

- **#6 Duplication**: resolved by the two shared helpers above.
- **#7 Cursor init inconsistency**: initialize `overviewScanNextCluster` once to `getFirstClusterIndexWithAudioData()` (so the scan never investigates header-only clusters); drop the redundant constructor `= 0` assignment and align the in-class initializer.

### Deferred / optional

- The `shiftRenderCache` sub-range black-flash for reversed / collapse-animation renders (cosmetic, one frame) — note in code, fix only if it proves visible.

## Testing / verification

There is no confirmed build/test harness in play yet, and these changes touch the live render path and the audio-idle path. Verification plan:

1. Build the firmware.
2. Exercise the issue repro (the long-song project referenced in #4460): confirm smooth vertical/horizontal scrolling in song row view under playback (the lag fix), and confirm the **final cluster** of long clips renders correctly (no spurious bright/inverted column) — the #1 regression.
3. Spot-check a sample whose audio length is an exact multiple of the cluster size (the `limit == 0` boundary case).

## Risk notes

- The shared-helper extraction touches the hot render path; before/after rendering must be visually identical for the common case. The extraction is mechanical (inner loop + store block), so risk is contained.
- The 64-bit widening at `findPeaksPerCol:340` is pre-existing behavior for normal-sized files (no change below 2 GB); only the overflow case changes.
