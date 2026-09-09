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
/// @brief Host-only C ABI for the document-save channel gate: save a preset through the REAL ENTER
///        gesture, require the written file to be whole, and report whether the storage channel
///        served the write off the owner.
///
/// ## Why this gate exists
///
/// `write_file` starts life with NO harness building it, which is exactly the position the whole-file
/// READ was in one rung earlier -- and running that for the first time found two defects at once. A
/// feature no gate builds is code that has never run.
///
/// ## Why the assertion is a CRC compared ACROSS the two legs
///
/// The gate's subject is which arm did the writing, so the property that matters is that both arms
/// write the SAME bytes. A leg checking only its own output proves the arm ran, not that it agrees
/// with the other one -- and disagreement is the defect worth catching.
///
/// The in-process checks are deliberately shallow (the file exists, opens with the XML prolog, is not
/// trivially short) because the cross-leg comparison is what carries the weight.
///
/// @note Runs as a STEP MACHINE: a staged save spans ticks by construction, so every check reads its
///       result on a LATER tick than the gesture that caused it.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app`. Inert unless called.
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

/// @brief Reset the stage machine. Call once, before the first dispatch_step().
void deluge_scenario_document_save_channel_begin(void);

/// @brief True once every stage has run and the summary has been printed.
bool deluge_scenario_document_save_channel_complete(void);

/// @brief Advance the scenario by one tick. Fire-and-forget; poll step_done() for completion.
void deluge_scenario_document_save_channel_dispatch_step(void);

/// @brief True once the work queued by the last dispatch_step() has returned.
bool deluge_scenario_document_save_channel_step_done(void);

/// @brief Number of in-process assertion failures. 0 = this run passed.
int32_t deluge_scenario_document_save_channel_failures(void);

} // extern "C"

#endif // DELUGE_HOST
