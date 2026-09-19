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

/// @file
/// @brief Drives a card ejection and re-insertion with samples STILL RESIDENT, and pins what
///        `AudioFileManager::cardReinserted()`'s walk does to each of them.
///
/// The estate had no coverage of that walk at all. `audio-load-ab` drives the same eject/re-insert
/// edges, but measured on 2026-09-19 it reaches `cardReinserted()` twice with
/// `residents=0 samples=0 referenced=0` — the edges fire and the loop body never runs. This scenario
/// exists to give the walk something to walk, so Phase 4f's migration has evidence that can see it.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness directory is globbed into the
///       app's sources).
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

/// @brief Reset the scenario to its first stage.
void deluge_scenario_card_reinsert_begin(void);

/// @brief True once the report has been printed.
bool deluge_scenario_card_reinsert_complete(void);

/// @brief Run one stage, and queue a marker op behind whatever it queued on the storage owner.
void deluge_scenario_card_reinsert_dispatch_step(void);

/// @brief True once the marker op queued by the last dispatch_step() has run.
bool deluge_scenario_card_reinsert_step_done(void);

/// @brief Number of in-process assertion failures. 0 = the run passed.
int32_t deluge_scenario_card_reinsert_failures(void);

} // extern "C"

#endif // DELUGE_HOST
