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

/// @brief Host-only C ABI for the offset-addressed I/O gate.
///
/// ## `OI-TRANSFER` writes THREE blocks, and that is the whole point
///
/// A `write_at` that truncated would leave only the final block, and a single-block round trip would
/// pass happily against exactly that bug — which is what this gate most risks, because
/// `efatfs_core::create_context` truncates unconditionally and the correct open mode looks like a
/// mistake. Three blocks at ascending offsets is what makes the guard real.
///
/// ## The tier count is not this gate's evidence
///
/// `smsysex` never begins a blocking operation after boot, so its tier count reads zero whichever
/// way its I/O runs. This gate's evidence is `OI-CHANNEL served=` going non-zero, plus the truncation
/// neutering failing.
extern "C" {

/// @brief Reset the scenario's state and prepare stdout for a piped run.
void deluge_scenario_offset_io_begin(void);

/// @brief Whether the summary has been printed and the run may stop stepping.
bool deluge_scenario_offset_io_complete(void);

/// @brief Run one tick of the scenario, then queue a marker behind whatever it dispatched.
void deluge_scenario_offset_io_dispatch_step(void);

/// @brief Whether the marker queued by the last dispatch has run.
bool deluge_scenario_offset_io_step_done(void);

/// @brief How many in-process assertions failed.
int32_t deluge_scenario_offset_io_failures(void);

} // extern "C"
