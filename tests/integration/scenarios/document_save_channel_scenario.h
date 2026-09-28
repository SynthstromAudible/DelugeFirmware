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
///        gesture, require the written file to be whole, and report whether the write channel
///        served the write after boot.
///
/// ## Why this gate exists
///
/// No other harness builds `write_file`. A feature no gate builds is code that has never run, so
/// this gate exists to run it and catch what only running finds.
///
/// ## Why the file check is shallow
///
/// The save verifies its own write by reading the document back through the channel, and only a save
/// that completed that read-back counts (DS-VERIFIED). The check on the file itself is deliberately
/// shallow (the file exists, opens with the XML prolog, is not trivially short): it catches the
/// shapes a verified save could still leave behind, and its CRC is what DS-OCCUPIED and the overwrite
/// legs compare the document against later.
///
/// @note Runs as a STEP MACHINE: a staged save spans ticks by construction, so every check reads its
///       result on a LATER tick than the gesture that caused it.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app`. Inert unless called.

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
