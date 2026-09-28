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

/// @brief Host-only C ABI for the sample-load tree gate: build a song, save it with collected media, load it back,
///        load a synth preset and a kit preset, and report where every holder's file came from.
///
/// Every fixture document is written by the firmware's own save and serialisation paths inside the scenario; none
/// is hand-written. Each load runs inside a load-trace window (see `storage/audio/load_trace.h`), whose lines are
/// printed as `LT <load> <event>` so a gate can compare the trace across a restructuring of the load path.
///
/// @note Runs as a step machine: each `dispatch_step()` runs one stage inline on the calling tick, and the platform
///       driver waits for `step_done()` -- which first runs any fixture builds the stage queued -- before dispatching
///       again.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness directory is globbed into the app's sources).
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

/// @brief Reset the scenario to its first stage.
void deluge_scenario_sample_load_tree_begin(void);

/// @brief True once the report has been printed.
bool deluge_scenario_sample_load_tree_complete(void);

/// @brief Run one stage inline, on the calling tick.
void deluge_scenario_sample_load_tree_dispatch_step(void);

/// @brief True once the last dispatch_step()'s stage has returned and every fixture build it queued has run, one per
///        call.
bool deluge_scenario_sample_load_tree_step_done(void);

/// @brief Number of in-process assertion failures. 0 = the run passed.
int32_t deluge_scenario_sample_load_tree_failures(void);

} // extern "C"

#endif // DELUGE_HOST
