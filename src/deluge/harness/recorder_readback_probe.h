/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

/// @brief Host-only C-ABI diagnostic: can a still-recording sample be read back through the same
///        region-port cursor real playback uses?
///
/// `deluge_harness_recorder_probe()` constructs a real `SampleRecorder`, feeds it a small amount of
/// deterministic audio (leaving it in `RecorderStatus::CAPTURING_DATA` — never finalized/closed, so
/// its Sample genuinely has no efatfs read handle, `SampleStream::efatfs_handle() == 0`, exactly the
/// "still-recording" condition), then opens a `DelugeSampleSource` cursor against that Sample's
/// `SampleStream` and calls `deluge_sample_region_acquire_ex(index=0)` — the exact same region-port
/// entry point `SampleLowLevelReader` uses for real playback (see `sample_low_level_reader.cpp`). The
/// returned `DelugeRegionState`: `SampleStream::make_read_source()` always returns an
/// `EfatfsReadSource` over a 0 handle for a still-recording Sample, so the read fails and the async
/// fill task re-queues it at lowest priority (`streaming_loader.rs`'s `fill_once`) rather than failing
/// outright -- the acquire is expected to settle on LOADING, uniformly, on every target.
///
/// `DELUGE_HOST`-only, same reach as `harness/streaming_scenario.h` (compiled into
/// deluge_host/deluge_render/deluge_loadcheck AND the Rust Embassy `host_app` build, never the ARM
/// device firmware). Leaks the `SampleRecorder`/`Sample` for the process's life — a one-shot
/// diagnostic run, not a long-lived service; acceptable here.
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

/// @brief Run the whole probe in one call and return the resulting region state.
/// @param numChannels Recording channel count (1 or 2).
/// @param numFrames   Frames of deterministic audio to feed before probing (must be small enough to
///                    stay within the first cluster so `index=0` below is meaningful).
/// @param pumpDrainTicks How many times to call the fiber drain (`cardRoutine()`) BEFORE probing —
///                    lets the caller compare "probe immediately" vs. "probe after N drain ticks"
///                    (relevant since `async_streaming_loader`'s fill is a background task that may
///                    need scheduler ticks to run between the acquire call and a retry).
/// @return The `DelugeRegionState` for cluster index 0 (`DELUGE_REGION_READY` == 1,
///         `DELUGE_REGION_LOADING` == 2, `DELUGE_REGION_UNAVAILABLE` == 3), or 0 if the recorder
///         itself could not be set up (a harness failure, not a routing-divergence finding).
/// Set the probe up and feed it audio. Returns 1 on success, 0 if setup failed.
///
/// Does NOT drain: the caller drives that with @ref deluge_harness_recorder_probe_drain_tick, one
/// tick per yield, so the storage driver can run between ticks. Draining in a tight loop here worked
/// only on the recorder's synchronous arm and is what blocked `Owner::run` going inline.
uint8_t deluge_harness_recorder_probe(uint8_t numChannels, uint32_t numFrames, uint32_t pumpDrainTicks);

/// One tick of the drain. Call it between yields, as many times as the caller wants to flush.
void deluge_harness_recorder_probe_drain_tick();

/// Open the region source over the still-recording Sample and take the first acquire.
/// @return the `DelugeRegionState`, or 0 if the probe was never set up.
uint8_t deluge_harness_recorder_probe_acquire();

/// @brief Re-query the SAME open cursor's state for index 0 without acquiring again — lets a caller
///        poll (e.g. after letting the host_app async executor run some more ticks) to see whether a
///        LOADING result ever resolves. Returns `DELUGE_REGION_UNAVAILABLE` (3) if no probe is open.
uint8_t deluge_harness_recorder_probe_poll();

/// @brief Tear down the probe's recorder/cursor (aborts the recording, closes the cursor). Safe to
///        call even if no probe is open.
void deluge_harness_recorder_probe_end();

/// @brief Regression probe: a per-cluster structure `finalizeRecordedFile()` grows must reach the
///        real cluster count once a normal recording finishes (see `finalizeRecordedFile()`'s
///        finalize-grow comment in sample_recorder.cpp). The finalize-time grow-only guard sizes
///        `Sample::overviewCache_`, which this probe measures (see its `_table_clusters()` accessor
///        below).
///
/// Builds a real `SampleRecorder`, feeds it `numFrames` of deterministic mono ramp audio spanning
/// several `Cluster::size` clusters, and drives it through `endSyncedRecording()` +
/// `cardRoutine()` all the way to `RecorderStatus::COMPLETE` -- i.e. `finalizeRecordedFile()` runs
/// for real. `allowFileAlterationAfter` is never set on a harness-built recorder (it defaults
/// false and only a caller like `AudioClip` opts a recorder into it), so this ALWAYS takes
/// finalizeRecordedFile()'s no-alteration else-branch -- the one branch the regression left
/// completely unresized, and the only branch AudioClip recording itself ever takes.
///
/// After finalize, opens a FRESH efatfs read handle on the finalized file (mirroring how a real
/// reload/playback reaches a just-recorded sample -- the recorder's own write context is already
/// closed by then) and acquires @p regionIndex through the exact `deluge_sample_source_*` region
/// port real playback uses.
///
/// @param numChannels Recording channel count (1 or 2); mono (1) guarantees the no-alteration
///                    branch independent of the `allowFileAlterationAfter` reasoning above.
/// @param numFrames   Frames of deterministic ramp audio to feed -- choose enough to span multiple
///                    `Cluster::size` clusters so @p regionIndex >= 1 is meaningful.
/// @param regionIndex The cluster index to acquire after finalize (>= 1 to probe the regression).
/// @return The `DelugeRegionState` for @p regionIndex (`DELUGE_REGION_READY` == 1,
///         `DELUGE_REGION_LOADING` == 2, `DELUGE_REGION_UNAVAILABLE` == 3), or 0 if the harness
///         itself could not set up (setup()/finalize/open-read-stream failure -- NOT a regression
///         finding).
uint8_t deluge_harness_recorder_finalized_multicluster_probe(uint8_t numChannels, uint32_t numFrames,
                                                             uint32_t regionIndex);

/// @brief Re-query/retry the SAME open cursor's acquire for the region index passed to the last
///        deluge_harness_recorder_finalized_multicluster_probe() call -- lets a caller (e.g. the
///        host_app Rust side, where the async fill task needs real executor yields between checks)
///        poll for a LOADING result to resolve. Returns `DELUGE_REGION_UNAVAILABLE` (3) if no
///        probe is open.
uint8_t deluge_harness_recorder_finalized_multicluster_probe_poll();

/// @return The waveform overview cache's physical entry count (`Sample::overviewCacheSize()`)
///         captured immediately after finalize (before any acquire) -- the direct assertion for "was
///         the finalize-time grow left under-sized". 0 if no probe has run (harness-error state, not
///         a valid measurement).
/// One tick of the finalized probe's pump. 0 = still going, 1 = COMPLETE, 2 = failed/bounded out.
/// The caller ticks this with a yield in between, so the storage driver can answer.
uint8_t deluge_harness_recorder_finalized_multicluster_probe_tick();

/// Acquire and verify, once the pump above has reported COMPLETE.
uint8_t deluge_harness_recorder_finalized_multicluster_probe_finish(uint32_t regionIndex);

uint32_t deluge_harness_recorder_finalized_multicluster_probe_table_clusters();

/// @return The finalized recording's true required cluster count for the same geometry
///         (`ceil((audioDataStartPosBytes + audioDataLengthBytes) / Cluster::size)`, the exact
///         formula finalizeRecordedFile()'s hoisted resize uses) -- what
///         deluge_harness_recorder_finalized_multicluster_probe_table_clusters() must be >= for
///         the invariant to hold. 0 if no probe has run.
uint32_t deluge_harness_recorder_finalized_multicluster_probe_expected_clusters();

/// @return 1 if the last acquire (from the initial call or a poll) resolved
///         `DELUGE_REGION_READY` AND the acquired region's payload bytes matched the deterministic
///         ramp this probe recorded, byte-for-byte. 0 otherwise (not READY, no probe has run, or a
///         genuine byte mismatch).
uint8_t deluge_harness_recorder_finalized_multicluster_probe_bytes_ok();

/// @brief Tear down the finalized-multicluster probe's recorder/cursor. Safe to call even if no
///        probe is open.
void deluge_harness_recorder_finalized_multicluster_probe_end();

} // extern "C"

#endif // DELUGE_HOST
