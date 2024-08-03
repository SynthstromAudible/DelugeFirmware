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

constexpr int32_t kMaxChordKeyboardSize = 4;
constexpr int32_t kUniqueChords = 8;

namespace deluge::gui::ui::keyboard {


struct Chord {
	const char* name;
	int32_t offsets[kMaxChordKeyboardSize];
};

struct Chords {
	Chord chords[kUniqueChords];
};

const Chords chords = {
	{
		{"", {0, 0, 0, 0}},
		{"MAJ", {4, 7, 0, 0}},
		{"MIN", {3, 7, 0, 0}},
		{"SUS2", {2, 7, 0, 0}},
		{"SUS4", {5, 7, 0, 0}},
		{"DOM7", {4, 7, 10, 0}},
		{"MAJ7", {4, 7, 11, 0}},
		{"MIN7", {3, 7, 10, 0}},
	}
};

}
