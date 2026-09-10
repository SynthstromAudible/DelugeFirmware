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
/// @brief Host-only C ABI for the song-save gate: build a song document in memory and place it on
///        the card through the channel, then require that what landed is a song and not merely
///        bytes.
///
/// ## Why the song has to be inflated first
///
/// The default song in this harness serialises to about 11.5 KB. The staging buffer starts at 32768
/// bytes, so saving that song never grows the buffer once, and every check about the growth path
/// would be running over code that cannot execute. `SS-GREW` reports the doubling count and fails
/// below two, so a fixture that quietly stops being big enough fails instead of passing.
///
/// ## The two checks worth their runtime
///
/// `SS-PARSES` loads the saved document back through the real loader. A staged write that produced
/// plausible bytes with a broken header or a missing closing tag would satisfy any comparison
/// between two staged builds -- both would be broken identically -- and only reading it back as a
/// song catches it.
///
/// `SS-INTACT` saves a second time over the first and requires that, when a save fails partway, the
/// song already on the card is still there and still loads. That is the property that justifies
/// writing to a temp name and renaming rather than writing straight over the target.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app`. Inert unless called.
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

void deluge_scenario_song_save_channel_begin(void);
bool deluge_scenario_song_save_channel_complete(void);
void deluge_scenario_song_save_channel_dispatch_step(void);
bool deluge_scenario_song_save_channel_step_done(void);
int32_t deluge_scenario_song_save_channel_failures(void);

} // extern "C"

#endif // DELUGE_HOST
