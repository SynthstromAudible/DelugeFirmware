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

/// @brief A song load takes every UI down for its whole length. While the UI stack is empty, input and UI timers
/// must reach no UI that was open before the load (SU-INERT), the arming gestures the load itself answers must still
/// reach it (SU-REPEATS, SU-HOLD) and its popup stay up under a knob turn (SU-POPUP), and once the outgoing song has
/// been swapped out nothing the empty stack reaches may still point into it (SU-RETIRED). Pads held when the UIs go
/// down are released through them (SU-AUDITION), and a load that fails after that puts the surviving song's UI back
/// (SU-FAILED).
extern "C" {
void deluge_scenario_stale_ui_begin(void);
bool deluge_scenario_stale_ui_complete(void);
void deluge_scenario_stale_ui_dispatch_step(void);
bool deluge_scenario_stale_ui_step_done(void);
int32_t deluge_scenario_stale_ui_failures(void);
} // extern "C"
