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

/// @brief Host-only C ABI for the container-guard probe. With playback running, each leg makes one structural edit to
///        a container the render walks -- a note, a note row, a clip, a clip instance, an automation node, a drum, an
///        output -- through the model's own entry point, and reports how many structural edits were made while the
///        render could have preempted them (deluge::unguardedStructuralEditsTotal(), util/structural_edit_probe.h).
///
/// @note The scenario fails nothing on a count: tests/integration/checks/container_guard.py judges them.
///
/// @note Runs as a step machine: each `dispatch_step()` does one stage, and the platform driver lets virtual time
///       advance between steps. The scenario drives the render itself.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness directory is globbed into the app's sources).

#include <cstdint>

extern "C" {

/// @brief Reset the scenario to its first leg.
void deluge_scenario_container_guard_begin(void);
/// @brief Whether the summary line has been printed: the run is over.
bool deluge_scenario_container_guard_complete(void);
/// @brief Run one stage.
void deluge_scenario_container_guard_dispatch_step(void);
/// @brief Whether the last stage has run.
bool deluge_scenario_container_guard_step_done(void);
/// @brief How many legs' fixtures could not be built or never played. The counts are judged by the script.
int32_t deluge_scenario_container_guard_failures(void);

} // extern "C"
