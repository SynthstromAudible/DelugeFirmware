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

/// @file
/// @brief The pattern-load gate: a pattern file loaded through the real pattern browser, after boot.
///
/// The boot song's synth clip is given notes, written to a pattern file with the firmware's own pattern writer, and
/// cleared. The pattern browser is then opened the way the clip view's LOAD gesture opens it, and the file is loaded
/// twice: once with Enter, and once previewed with PLAY. Each leg must put back exactly the notes that were written,
/// and neither may make a synchronous card call on the interaction tier: every byte of the pattern has to arrive
/// through the channels.
extern "C" {

/// @brief Reset the scenario's state and prepare stdout for a piped run.
void deluge_scenario_pattern_load_begin(void);

/// @brief Whether the summary has been printed and the run may stop stepping.
bool deluge_scenario_pattern_load_complete(void);

/// @brief Run one tick of the scenario.
void deluge_scenario_pattern_load_dispatch_step(void);

/// @brief Whether the last dispatched tick has finished.
bool deluge_scenario_pattern_load_step_done(void);

/// @brief How many in-process assertions failed.
int32_t deluge_scenario_pattern_load_failures(void);

} // extern "C"
