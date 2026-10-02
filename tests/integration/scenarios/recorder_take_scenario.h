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

/// @brief Audio-input recording takes, each started from the sound editor's recorder menu item. A take runs while the
/// rest of the app keeps going: DIN MIDI notes and clock delivered during it are handled during it (RT-TAKE), and BACK
/// attaches its sample to the source it was recorded for (RT-ATTACH). No take starts while a load that can replace the
/// clip's output is in flight (RT-REFUSED); a take whose recorder UI is closed under it (RT-CLOSED), or whose synth is
/// replaced under it (RT-REPLACED), is dropped and attached to nothing, and the next take starts (RT-SECOND).
extern "C" {
void deluge_scenario_recorder_take_begin(void);
bool deluge_scenario_recorder_take_complete(void);
void deluge_scenario_recorder_take_dispatch_step(void);
bool deluge_scenario_recorder_take_step_done(void);
int32_t deluge_scenario_recorder_take_failures(void);
} // extern "C"
