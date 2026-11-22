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

#include "music/musical_key.h"
#include "music/note_set.h"
#include "music/preset_scales.h"

#include <ostream>

/// @file
/// @brief Prints a set of notes, a preset scale and a musical key in a failed CppSpec comparison, alone or as fields
///        of a record that holds them.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::music {

/// @brief Prints a set as chromatic note names from C, e.g. `NoteSet(C, E, F)`; a set does not name its root, so the
///        names read as offsets from it.
/// @param os The stream to print to.
/// @param s The set.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const NoteSet& s) {
	static constexpr const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	os << "NoteSet(";
	const char* separator = "";
	for (int8_t note = 0; note < NoteSet::size; ++note) {
		if (s.has(note)) {
			os << separator << kNames[note];
			separator = ", ";
		}
	}
	return os << ")";
}

/// @brief Prints a scale by its name, e.g. `MAJOR`: the enum is a `uint8_t`, which would otherwise print as a raw byte.
/// @param os The stream to print to.
/// @param scale The scale.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, Scale scale) {
	return os << getScaleName(scale);
}

/// @brief Prints a key as its root note and its mode's notes, e.g. `{root 0, mode NoteSet(C, D, E, F, G, A, B)}`.
/// @param os The stream to print to.
/// @param k The key.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const MusicalKey& k) {
	return os << "{root " << k.rootNote << ", mode " << k.modeNotes << "}";
}

} // namespace deluge::music
