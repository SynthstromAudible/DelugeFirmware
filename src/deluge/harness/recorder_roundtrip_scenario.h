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

/// @brief Host-only C-ABI for the SampleRecorder byte-exact round-trip oracle, shared so it can run
///        BOTH on the C-host `deluge_recorder_roundtrip` binary AND on the Rust/Embassy host BSP
///        (golden_vt_render's `GOLDEN_SCENARIO=recorder_roundtrip` mode).
///
/// The whole geometry matrix + the two readback probes, minus the
/// host-side image formatting and process exit (the caller owns those). Assumes `DELUGE_SD_IMAGE`
/// is already mounted (boot mounts it). Reads back through `deluge::io::File` (drain-independent)
/// except the `finalized_multicluster` probe, which the caller polls, asking the async fill task for a drain
/// each time — see `recorder_readback_probe.cpp`. Every stage is driven one step per call, by the caller's ticks.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness dir is globbed in), so the
///       Embassy binary — which links `deluge_app`, not `host_recorder_roundtrip_main.cpp` — can
///       reach it. Inert unless called.
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

/// @brief Set the round-trip up: print RW-ACTIVE and the cluster size, and build the case list. Blocks on
///        nothing; call inline, then tick `deluge_scenario_recorder_cases_tick()`.
/// @return 0.
int32_t deluge_scenario_run_recorder_roundtrip();

/// @brief Start the oracle's tail -- the live-readback probe, the finalized probe's acquire, the summary -- once the
///        cases and the finalized probe's pump are done.
void deluge_scenario_recorder_rest_begin();

/// @brief One step of the tail. Call with a yield between calls: the probes' drains are requests, answered in between.
/// @return 0 while it runs, 1 once RW-RESULT has printed.
int32_t deluge_scenario_recorder_rest_tick();

/// @brief Report the oracle's failure count so far.
/// @return The oracle's failure count so far (0 = all passed); final once the tail has returned 1.
int32_t deluge_scenario_recorder_roundtrip_failures();

/// @brief Set up the off-owner recording case.
///
/// The cases above ride whichever arm their recorder picks; this one is driven entirely by the caller's ticks,
/// the shape the device uses, so the async cluster write and the recorder's blocking ticks are
/// exercised where the device drives them.
/// @param mode Which case:
///             - 0 `recording`: mono, finished normally.
///             - 1 `alterfile`: stereo with a silent right channel and alteration permitted, so finalize
///               takes its REMOVE_RIGHT_CHANNEL branch. No other case permits alteration, so without this
///               the batch machine is never run.
///             - 2 `abort`: abandoned part-way, so the abort machine runs.
///             - 3 `busy`: another write-channel operation keeps taking the channel, so the recorder's
///               requests are refused and asked again. Prints `RW-BUSY`.
///             - 4 `stall`: a cluster write is held past the recorder's bound, so the take fails without a
///               synchronous write. Prints `RW-STALL`.
void deluge_scenario_offowner_begin(int32_t mode);

/// @brief Advance the off-owner case by one tick. Call from the same context as the begin.
///
/// Tick-driven on purpose: the pump must return so the storage owner can run the ticks this case
/// hands it and the Rust write task can make progress.
/// @return 0 while running, 1 when it passed, 2 when it failed.
int32_t deluge_scenario_offowner_tick(void);

/// @brief Print `RW-TIER`: how many synchronous card operations the recorder began off the storage owner
///        after boot, and how many of its channel requests were refused, over every off-owner case.
///
/// Process-wide on purpose: a synchronous operation is a violation whichever case made it. Call once,
/// after the last case.
void deluge_scenario_offowner_report(void);

} // extern "C"

#endif // DELUGE_HOST
