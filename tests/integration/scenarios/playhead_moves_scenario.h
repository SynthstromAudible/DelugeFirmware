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

/// @brief Host-only C ABI for the playhead-moves probe. The render ticks the sequencer, so the interaction tier may
///        move the playhead -- a lane cursor, a clip's or note row's position, the next-event schedule, the playback
///        mode and state -- only with the render excluded, and must not allocate or log undo while it holds it off.
///        Each leg makes one move through the app's own entry point while a clock runs (or starts one) and reports
///        PM-<LEG> stray=<n> masked_alloc=<n> masked_actions=<n> hash=<h>; the injection legs run a render block at a
///        point inside a move and report what that block saw.
///
/// @note The scenario fails nothing on a count: tests/integration/checks/playhead_moves.py judges them.
///
/// @note Runs as a step machine: each `dispatch_step()` does one stage. The scenario drives the render itself.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness directory is globbed into the app's sources).

#include <cstdint>

extern "C" {

/// @brief Reset the scenario to its first leg.
void deluge_scenario_playhead_moves_begin(void);
/// @brief Whether the summary line has been printed: the run is over.
bool deluge_scenario_playhead_moves_complete(void);
/// @brief Run one stage.
void deluge_scenario_playhead_moves_dispatch_step(void);
/// @brief Whether the last stage has run.
bool deluge_scenario_playhead_moves_step_done(void);
/// @brief How many legs' fixtures could not be built or never played. The counts are judged by the script.
int32_t deluge_scenario_playhead_moves_failures(void);

} // extern "C"
