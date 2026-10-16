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

/// @brief A level-3 UI script (`UI_SCRIPT`) played into the whole app through the BSP's own input path, one virtual
/// millisecond at a time: every step due by now, then every snapshot due (`UI_SNAPSHOT_DIR`). It completes when the
/// script's `quit` has run and prints `UI-SCRIPT quit at <ms> ms`; a script that fails ends the run with exit 2.
extern "C" {
void deluge_scenario_ui_script_begin(void);
bool deluge_scenario_ui_script_complete(void);
void deluge_scenario_ui_script_dispatch_step(void);
bool deluge_scenario_ui_script_step_done(void);
int32_t deluge_scenario_ui_script_failures(void);
} // extern "C"
