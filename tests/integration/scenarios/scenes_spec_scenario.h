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

#include <cstdint>

/// @brief One scenes spec file (`SCENES_SPEC`, its id under sim/scenes_spec) run once, from the app-init hook, so
/// before boot installs a root UI, on the card `SCENES_SPEC_CARD` names. Prints `SCENES-SPEC <id> exit=<n>`; fails when
/// the file's entry returned non-zero. `SCENES_SPEC=--list` prints every id as `SCENES-SPEC-ID <id>` and exits.
extern "C" {
void deluge_scenario_scenes_spec_on_app_init(void);
void deluge_scenario_scenes_spec_begin(void);
bool deluge_scenario_scenes_spec_complete(void);
void deluge_scenario_scenes_spec_dispatch_step(void);
bool deluge_scenario_scenes_spec_step_done(void);
int32_t deluge_scenario_scenes_spec_failures(void);
} // extern "C"
