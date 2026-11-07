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

class InstrumentClip;
class Song;

/// An arrangement take's repeats. While the arrangement records a clip, its take repeats it at every loop end: the
/// render appends the repeat's notes and length there and counts it, and the interaction tier appends the repeat's
/// automation into the model before its next job, so the render neither writes a model lane nor makes a collection.
namespace deluge::modulation {

/// @brief [render] or [interaction] A repeat of @p take's source was appended to @p take (its notes and its length):
///        the interaction tier appends its automation at its next commit.
void countRepeat(InstrumentClip& take);

/// @brief [interaction] Append into the model the automation of each repeat counted for each arrangement take of
///        @p song and not yet appended, oldest first, at the length the model's automation covers. First thing in
///        every commit of recording, so a working copy is committed before its lane is appended to.
void catchUpArrangementTakes(Song& song);

/// @brief [render] @p clip is going: no take repeats it any more, and a repeat counted for one is not appended.
void forgetRepeatSource(Song& song, const InstrumentClip& clip);

} // namespace deluge::modulation
