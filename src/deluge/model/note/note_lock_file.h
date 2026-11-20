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

#include "model/note/note_locks.h"
#include "modulation/params/param.h"
#include <array>
#include <cstdint>

class Deserializer;
class NoteVector;
class Serializer;

/// Parameter locks in the song file: two noteRow attributes, written after the row's note data.
///   lockParams="lpfFrequency,volume"  - the locked params' file names, in "column" order
///   noteLocks="0x..."                 - per locked note: pos (8 hex), number of locks (2 hex), then per lock: column
///                                       (2 hex) and value (8 hex, the param's preset value)
/// Firmware that doesn't know these attributes skips them and plays the notes unlocked.

namespace deluge::note_locks {

/// Writes both attributes for a row's notes - or nothing, if none of them has locks
void writeToFile(Serializer& writer, NoteVector& notes, const Pool& pool);

/// Reads a row's two attributes, which arrive one at a time
class FileReader {
public:
	/// The lockParams attribute
	void readParams(Deserializer& reader);
	/// The noteLocks attribute: gives `notes` (already read) their locks, interned in `pool`
	void readLocks(Deserializer& reader, NoteVector& notes, Pool& pool) const;

private:
	// An unknown name (from some future firmware) still takes its column, so later columns line up; its locks are
	// dropped
	static constexpr uint8_t kUnknownParam = 0xFF;
	std::array<uint8_t, deluge::modulation::params::FIRST_GLOBAL> columnParams_{};
	int32_t numColumns_{0};
};

} // namespace deluge::note_locks
