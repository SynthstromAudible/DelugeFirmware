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
/// @brief Host-only C ABI for the song-load channel gate: save a song carrying a distinctive value,
///        load it back through the REAL `LoadSongUI`, and require both that the value returns and
///        that the read went through the storage channel, off the owner.
///
/// ## Why this gate exists at all
///
/// `scripts/recorder_write.sh` was the ONLY harness that built `async_file_write_active`. The
/// whole-file staging read the song load now depends on had therefore never executed under any gate:
/// it could have been broken in any way at all and every gate would still have been green, because
/// every one of them ran the build where that code is compiled out. This gate runs it.
///
/// ## Why the assertion is a VALUE from inside the song
///
/// The two cheaper signals are both known-bad. The song's NAME is written by `performSave` from the
/// entered text, so it matches whether or not the load happened. Object IDENTITY -- "is `currentSong`
/// a different object afterwards" -- returns false even on a clean tree in the save round-trip's
/// shape, so it cannot separate a real load from a retained one either.
///
/// `insideWorldTickMagnitude` is saved as `inputTickMagnitude` and read back by `Song::readFromFile`,
/// so it can only be right if bytes came off the card and were parsed. The scenario deliberately
/// stamps a DIFFERENT value over it after saving and before loading: without that, the value could be
/// correct merely because nothing ever changed it.
///
/// @note Runs as a STEP MACHINE: the load is dispatched onto the storage owner and staged across
///       ticks, so every check reads its result on a LATER tick than the gesture that caused it.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness directory is globbed into the
///       app's sources). Inert unless called.
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

/// @brief Reset the stage machine. Call once, before the first dispatch_step().
void deluge_scenario_song_load_channel_begin(void);

/// @brief True once every stage has run and the summary has been printed.
bool deluge_scenario_song_load_channel_complete(void);

/// @brief Advance the scenario by one tick. Fire-and-forget; poll step_done() for completion.
void deluge_scenario_song_load_channel_dispatch_step(void);

/// @brief True once the work queued by the last dispatch_step() has returned.
bool deluge_scenario_song_load_channel_step_done(void);

/// @brief Number of in-process assertion failures. 0 = this run passed.
int32_t deluge_scenario_song_load_channel_failures(void);

} // extern "C"

#endif // DELUGE_HOST
