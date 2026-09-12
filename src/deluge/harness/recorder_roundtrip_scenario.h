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
/// The whole geometry matrix + alterFile characterization + the two readback probes, minus the
/// host-side image formatting and process exit (the caller owns those). Assumes `DELUGE_SD_IMAGE`
/// is already mounted (boot mounts it). Reads back through `deluge::io::File` (drain-independent)
/// except the `finalized_multicluster` probe, whose fill drain routes unconditionally onto
/// `deluge_streaming_drain_queue_blocking()` (the async fill task) — see `recorder_readback_probe.cpp`.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness dir is globbed in), so the
///       Embassy binary — which links `deluge_app`, not `host_recorder_roundtrip_main.cpp` — can
///       reach it. Inert unless called.
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

/// @brief Run the whole recorder round-trip matrix synchronously on the CURRENT context (the
///        storage-owner worker fiber on Embassy; the cooperative host driver on C-host), so its
///        file I/O — recorder writes, `deluge::io::File` reads, `open_read_stream` — can block on
///        that context.
/// @return Number of failed cases (0 = all passed).
int32_t deluge_scenario_run_recorder_roundtrip();

/// @brief Set up the off-owner recording case. Call from a NON-fiber context.
///
/// The eleven cases in @ref deluge_scenario_run_recorder_roundtrip all run on the storage owner,
/// because the oracle is dispatched there wholesale for the finalized probe's streaming drain. This
/// case exists to cover what that cannot: a recorder driven from where the device actually drives
/// one, so the async cluster write is exercised and `cardRoutine`'s dispatch of its BLOCKING ticks
/// back to the owner is load-bearing rather than dead code.
/// @param alter non-zero to record stereo with a silent right channel and permit alteration, so
///              finalize takes its REMOVE_RIGHT_CHANNEL branch. The three alterFile cases in the
///              oracle call `alterFile()` DIRECTLY, so without this the batch machine is never run.
void deluge_scenario_offowner_begin(int32_t alter);

/// @brief Advance the off-owner case by one tick. Call from the SAME non-fiber context.
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
