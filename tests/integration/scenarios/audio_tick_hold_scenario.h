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

/// @brief The audio tick's hold and re-entry guard (`AudioEngine::AudioTickHold`, `routine()`), run as
///        `GOLDEN_SCENARIO=audio_tick_hold` on the booted app.
///
/// `begin` runs every check and prints one line per check:
///     ATH-IDLE     no hold alive: audioTicksHeld() is false
///     ATH-HOLD     a hold is held for its scope, and released after it
///     ATH-NESTED   nested holds release only when the outermost ends
///     ATH-HELD     a held tick does nothing: routine_task() is false, routine() 0, the routine body never reached
///     ATH-REENTRY  routine() called from inside routine() returns 0 without running the body
///     ATH-THROW    a throw out of routine() does not latch it: the next tick reaches the routine body
/// then `ATH-RESULT failures=N`.
extern "C" {

/// @brief Run every check.
void deluge_scenario_audio_tick_hold_begin(void);

/// @brief Always true: `begin` does all the work.
bool deluge_scenario_audio_tick_hold_complete(void);

/// @brief Nothing to step.
void deluge_scenario_audio_tick_hold_dispatch_step(void);

/// @brief Always true.
bool deluge_scenario_audio_tick_hold_step_done(void);

/// @brief How many checks failed.
int32_t deluge_scenario_audio_tick_hold_failures(void);

} // extern "C"
