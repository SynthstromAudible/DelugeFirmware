/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"

constexpr int32_t kMaxChordKeyboardSize = 6;
constexpr int32_t kUniqueVoicings = 3;
constexpr int32_t kUniqueChords = 17;

namespace deluge::gui::ui::keyboard {

struct Voicing {
	int32_t offsets[kMaxChordKeyboardSize];
};

struct Chord {
	const char* name;
	Voicing voicings[kUniqueVoicings] = {0};
};

class Chords {
public:
	Chords();
	Voicing getVoicing(int32_t chordNo);

	Chord chords[kUniqueChords];
	int32_t voicingOffset[kUniqueChords] = {0};
};

} // namespace deluge::gui::ui::keyboard

// extern deluge::gui::ui::keyboard::Chords2 chords2;
