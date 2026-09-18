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

/// @brief Host-only C ABI for the clear-song/undo gate: Create New Song and, later, undo and redo -- the two
///        operations Phase 4c takes off the storage owner.
///
/// @note Runs as a step machine: each `dispatch_step()` does one stage and queues a marker op on the storage owner,
///       and the platform driver waits for `step_done()` before dispatching again.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness directory is globbed into the app's sources).
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

/// @brief Reset the scenario to its first stage.
void deluge_scenario_clear_song_undo_begin(void);
/// @brief Whether the summary line has been printed: the run is over.
bool deluge_scenario_clear_song_undo_complete(void);
/// @brief Run one stage, and queue the marker op the driver waits on.
void deluge_scenario_clear_song_undo_dispatch_step(void);
/// @brief Whether the marker op behind the last stage has run.
bool deluge_scenario_clear_song_undo_step_done(void);
/// @brief How many checks failed.
int32_t deluge_scenario_clear_song_undo_failures(void);

} // extern "C"

#endif // DELUGE_HOST
