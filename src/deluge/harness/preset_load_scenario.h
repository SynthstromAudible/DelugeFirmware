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

/// @brief Host-only C ABI for the preset-load gate: load synth and kit presets through the real preset browser into a
///        clip, a kit row and a clone, and report what each load bound and on which tier its card work began.
///
/// @note Runs as a step machine: each `dispatch_step()` does one stage and queues a marker op on the storage owner,
///       and the platform driver waits for `step_done()` before dispatching again.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness directory is globbed into the app's sources).
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

/// @brief Reset the scenario to its first stage.
void deluge_scenario_preset_load_begin(void);

/// @brief True once the report has been printed.
bool deluge_scenario_preset_load_complete(void);

/// @brief Run one stage, and queue a marker op behind whatever it queued on the storage owner.
void deluge_scenario_preset_load_dispatch_step(void);

/// @brief True once the marker op queued by the last dispatch_step() has run.
bool deluge_scenario_preset_load_step_done(void);

/// @brief Number of in-process assertion failures. 0 = the run passed.
int32_t deluge_scenario_preset_load_failures(void);

} // extern "C"

#endif // DELUGE_HOST
