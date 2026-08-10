/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

struct ComparativeNoteNumber {
	int32_t noteNumber;
	int32_t stringLength;
};

// You must set this at some point before calling strcmpspecial. This isn't implemented as an argument because
// sometimes you want to set it way up the call tree, and passing it all the way down is a pain.
extern bool shouldInterpretNoteNames;

extern bool octaveStartsFromA; // You must set this if setting shouldInterpretNoteNames to true.

// Returns 100000 if the string is not a note name.
// The returned number is *not* a MIDI note. It's arbitrary, used for comparisons only.
// noteChar has been made lowercase, which is why we can't just take it from the string.
ComparativeNoteNumber getComparativeNoteNumberFromChars(char const* string, char noteChar, bool octaveStartsFromA);

/// Orders filenames the way the browser lists them: like strcmp, except that runs of digits compare as numbers rather
/// than character-by-character, so "SONG2" sorts before "SONG10".
// Returns positive if first > second
// Returns negative if first < second
int32_t strcmpspecial(char const* first, char const* second);
