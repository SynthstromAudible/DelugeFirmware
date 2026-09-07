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

/// @brief Host-only C ABI for the save round-trip gate: save a song through the REAL `SaveSongUI`
///        gesture, load it back, save it again, and require the two written files to be identical.
///
/// ## Why a round trip rather than a golden
///
/// The property that matters is that a saved song can be loaded and re-saved without losing
/// anything, and that is self-checking: nothing here is baselined, so there is no golden anyone can
/// quietly re-baseline into passing.
///
/// ## Why the second save overwrites the SAME name
///
/// `performSave` writes `currentSong->name = enteredText` as part of a successful save, so saving
/// the reloaded song under a DIFFERENT name would legitimately produce different bytes and the
/// comparison would fail for a reason that is not a defect. Overwriting the same name keeps every
/// name-derived field identical, so a difference can only come from the round trip losing something.
/// It also makes the second save the overwrite-prompt case, so one gesture covers both properties.
///
/// ## The seam is asserted by OUTCOME
///
/// Whether the overwrite context menu opened is read from the UI itself, not from a dispatch count.
/// A count proves only that an op was queued -- this gate exists to prove the save actually
/// happened and the prompt actually appeared.
///
/// @note Runs as a STEP MACHINE, because saving is dispatched onto the storage owner: an op is only
///       ENQUEUED when it is dispatched and runs on a later worker poll, so every check reads its
///       result on a LATER tick than the gesture that caused it.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness directory is globbed into the
///       app's sources). Inert unless called.
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

/// @brief Reset the stage machine. Call once, before the first dispatch_step().
void deluge_scenario_save_roundtrip_begin(void);

/// @brief True once every stage has run and the summary has been printed.
bool deluge_scenario_save_roundtrip_complete(void);

/// @brief Advance the scenario by one tick. Fire-and-forget; poll step_done() for completion.
void deluge_scenario_save_roundtrip_dispatch_step(void);

/// @brief True once the work queued by the last dispatch_step() has returned.
bool deluge_scenario_save_roundtrip_step_done(void);

/// @brief Number of in-process assertion failures. 0 = this run passed.
int32_t deluge_scenario_save_roundtrip_failures(void);

} // extern "C"

#endif // DELUGE_HOST
