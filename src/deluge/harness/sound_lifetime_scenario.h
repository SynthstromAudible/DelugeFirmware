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

/// @brief Host-only C ABI for the sound-lifetime probe: a sounding synth and a sounding kit are deleted through each
///        real deletion path, and each path reports how many Sounds it destroyed while they still had live voices.
///
/// @note Runs as a step machine: each `dispatch_step()` does one stage, and the platform driver lets virtual time
///       (and so the audio render) advance between steps.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness directory is globbed into the app's sources).
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

/// @brief Reset the scenario to its first stage.
void deluge_scenario_sound_lifetime_begin(void);
/// @brief Whether the summary line has been printed: the run is over.
bool deluge_scenario_sound_lifetime_complete(void);
/// @brief Run one stage.
void deluge_scenario_sound_lifetime_dispatch_step(void);
/// @brief Whether the last stage has run.
bool deluge_scenario_sound_lifetime_step_done(void);
/// @brief How many fixtures could not be built or never sounded, deletions left objects waiting to be destroyed at
///        their report, or retired objects were never freed. The destroyed-with-live-voices counts are checked by
///        the script.
int32_t deluge_scenario_sound_lifetime_failures(void);

} // extern "C"

#endif // DELUGE_HOST
