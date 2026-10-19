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

/// @brief Instrumentation for the streaming-underrun harness's PRIMARY signal: the
///        audio thread reaching a region-port NotReady residency check (`!hasCurrentRegion()`,
///        or `deluge_sample_region_state` returning LOADING) on a play-needed path and either
///        deferring (WAIT) or dropping (UNASSIGN) the voice as a result.
///
/// Not a fault, not a blocking wait — just the exact moment playback discovers the region it
/// needs isn't ready yet.
///
/// Compiled where `DELUGE_UNDERRUN_COUNTERS` is 1: every x86 build off this tree (`DELUGE_HOST`, see
/// `sim/CMakeLists.txt`) and the device's stream-bench image (`DELUGE_STREAM_BENCH`), never the shipping
/// firmware. Every call site (`model/voice/voice_sample.cpp`, `model/sample/sample_low_level_reader.cpp`) is
/// itself wrapped in `#if DELUGE_UNDERRUN_COUNTERS` (not just relying on these being no-ops), so a shipping
/// build doesn't even see the calls -- behaviour there is unaffected.
#if defined(DELUGE_HOST) || DELUGE_STREAM_BENCH
#define DELUGE_UNDERRUN_COUNTERS 1
#else
#define DELUGE_UNDERRUN_COUNTERS 0
#endif

#if DELUGE_UNDERRUN_COUNTERS

#include <cstdint>

namespace deluge::harness {

/// @brief Count one WAIT-class underrun miss.
///
/// `VoiceSample::attemptLateSampleStart` found the start region still LOADING (or, with the start
/// region ready, its neighbour still LOADING) on a play-needed path and is about to return
/// `LateStartAttemptStatus::WAIT` (defer, come back later) rather than starting playback.
/// Called from exactly one place — the shared fall-through just above the `WAIT` return —
/// since both residency misses converge there.
void noteUnderrunWait();

/// @brief Count one UNASSIGN-class underrun miss.
///
/// A voice found the Cluster it needs isn't loaded on a path that returns `false` all the way
/// up to `Voice::render`'s `goto instantUnassign` — a dropped voice, not just a deferral. Two
/// call sites converge here, both the same severity:
///   - `VoiceSample::stopReadingFromCache`: the repitch-cache-stop path (only reached by
///     voices actively using the timestretch/repitch `SampleCache`).
///   - `SampleLowLevelReader::moveOnToNextCluster`: ordinary (non-cache, non-time-stretch)
///     forward playback crossing a Cluster boundary mid-stream and finding the next Cluster
///     not loaded — the common-case sustained-streaming underrun, and the one most voices
///     actually hit (most playback never uses the repitch cache at all). Without this site the
///     harness's counters could stay at zero even under genuine SD-latency starvation of
///     ordinary sustained streaming, because the miss would silently unassign the voice via a
///     wholly uninstrumented path.
void noteUnderrunUnassign();

/// @brief Count one sample-voice render (`VoiceSample::render`), by the tier running it.
///
/// Evidence that a render really played samples, and where: the device renders sample voices on the audio tier,
/// an offline export on the interaction tier.
void noteVoiceRender();

} // namespace deluge::harness

extern "C" {

/// @brief Total WAIT-class underrun misses observed since boot; see
/// deluge::harness::noteUnderrunWait.
///
/// Monotonic — callers baseline-subtract across a window, same pattern as `sd.rs`'s
/// `stats::reads()`/`stats::writes()`.
/// @return Cumulative WAIT-class miss count.
uint64_t deluge_sim_underrun_wait_count();

/// @brief Total UNASSIGN-class underrun misses observed since boot; see
/// deluge::harness::noteUnderrunUnassign.
/// @return Cumulative UNASSIGN-class miss count.
uint64_t deluge_sim_underrun_unassign_count();

/// @brief Sample-voice renders since boot on tier @p tier (a `DELUGE_TIER_*` value); see
/// deluge::harness::noteVoiceRender. Zero for a tier out of range.
uint64_t deluge_sim_voice_render_count(uint8_t tier);

} // extern "C"

#endif // DELUGE_UNDERRUN_COUNTERS
