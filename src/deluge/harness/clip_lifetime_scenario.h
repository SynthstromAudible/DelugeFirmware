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

/// @brief Host-only C ABI for the clip-lifetime probe: a playing clip is deleted through each real clip deletion path,
///        and each path reports how many Clips it destroyed while something the render reads between blocks still
///        pointed at them (Clip::clipsDestroyedWhileReferenced()).
///
/// @note Report-only: the counts are evidence for the paths that must retire a clip rather than destroy it, and no
///       count fails the run.
///
/// @note Runs as a step machine: each `dispatch_step()` does one stage, and the platform driver lets virtual time
///       advance between steps. The scenario drives the render itself.
///
/// @note Setting `CLIP_LIFETIME_FROM=<LEG>` starts the run at that leg, so the script can carry on past a leg that
///       ended the process.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness directory is globbed into the app's sources).
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

/// @brief Reset the scenario to its first stage (or the leg `CLIP_LIFETIME_FROM` names).
void deluge_scenario_clip_lifetime_begin(void);
/// @brief Whether the summary line has been printed: the run is over.
bool deluge_scenario_clip_lifetime_complete(void);
/// @brief Run one stage.
void deluge_scenario_clip_lifetime_dispatch_step(void);
/// @brief Whether the last stage has run.
bool deluge_scenario_clip_lifetime_step_done(void);
/// @brief How many legs' fixtures could not be built or never played. The counts themselves fail nothing.
int32_t deluge_scenario_clip_lifetime_failures(void);

} // extern "C"

#endif // DELUGE_HOST
