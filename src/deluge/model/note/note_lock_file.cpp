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

#include "model/note/note_lock_file.h"
#include "model/note/note.h"
#include "model/note/note_vector.h"
#include "storage/storage_manager.h"
#include "util/d_string.h"
#include "util/functions.h"
#include <algorithm>
#include <string_view>

namespace params = deluge::modulation::params;

namespace deluge::note_locks {

void writeToFile(Serializer& writer, NoteVector& notes, const Pool& pool) {
	// A column for each param any note locks, in order of first use
	std::array<uint8_t, params::FIRST_GLOBAL> columnParams;
	std::array<int8_t, params::FIRST_GLOBAL> columnOfParam;
	columnOfParam.fill(-1);
	int32_t numColumns = 0;
	for (int32_t n = 0; n < notes.getNumElements(); n++) {
		for (uint8_t param : pool.get(notes.getElement(n)->lockSet).params()) {
			if (columnOfParam[param] < 0) {
				columnOfParam[param] = numColumns;
				columnParams[numColumns++] = param;
			}
		}
	}
	if (numColumns == 0) {
		return;
	}

	writer.insertCommaIfNeeded();
	writer.write("\n");
	writer.printIndents();
	writer.writeTagNameAndSeperator("lockParams");
	writer.write("\"");
	for (int32_t c = 0; c < numColumns; c++) {
		if (c != 0) {
			writer.write(",");
		}
		writer.write(params::paramNameForFile(params::Kind::PATCHED, columnParams[c]));
	}
	writer.write("\"");

	writer.insertCommaIfNeeded();
	writer.write("\n");
	writer.printIndents();
	writer.writeTagNameAndSeperator("noteLocks");
	writer.write("\"0x");
	for (int32_t n = 0; n < notes.getNumElements(); n++) {
		Note* note = notes.getElement(n);
		LockSet locks = pool.get(note->lockSet);
		if (locks.empty()) {
			continue;
		}
		char buffer[9];
		intToHex(note->pos, buffer);
		writer.write(buffer);
		intToHex(locks.size(), buffer, 2);
		writer.write(buffer);
		for (int32_t l = 0; l < locks.size(); l++) {
			intToHex(columnOfParam[locks.params()[l]], buffer, 2);
			writer.write(buffer);
			intToHex(locks.values()[l], buffer);
			writer.write(buffer);
		}
	}
	writer.write("\"");
}

void FileReader::readParams(Deserializer& reader) {
	numColumns_ = 0;
	String names;
	if (reader.readTagOrAttributeValueString(&names) != Error::NONE) {
		return;
	}
	std::string_view rest{names.get()};
	while (!rest.empty() && numColumns_ < static_cast<int32_t>(columnParams_.size())) {
		std::string_view name = rest.substr(0, rest.find(','));
		rest.remove_prefix(std::min(name.size() + 1, rest.size()));

		char buffer[48];
		size_t length = name.copy(buffer, sizeof(buffer) - 1);
		buffer[length] = 0;
		int32_t param = params::fileStringToParam(params::Kind::PATCHED, buffer, true);
		columnParams_[numColumns_++] = isLockableParam(param) ? param : kUnknownParam;
	}
}

void FileReader::readLocks(Deserializer& reader, NoteVector& notes, Pool& pool) const {
	if (numColumns_ == 0 || !reader.prepareToReadTagOrAttributeValueOneCharAtATime()) {
		return;
	}
	char const* firstChars = reader.readNextCharsOfTagOrAttributeValue(2);
	if (firstChars == nullptr || *(uint16_t*)firstChars != charsToIntegerConstant('0', 'x')) {
		return;
	}

	while (true) {
		char const* hexChars = reader.readNextCharsOfTagOrAttributeValue(10);
		if (hexChars == nullptr) {
			return;
		}
		int32_t pos = hexToIntFixedLength(hexChars, 8);
		int32_t count = hexToIntFixedLength(&hexChars[8], 2);

		LockSet locks;
		for (int32_t l = 0; l < count; l++) {
			hexChars = reader.readNextCharsOfTagOrAttributeValue(10);
			if (hexChars == nullptr) {
				return;
			}
			int32_t column = hexToIntFixedLength(hexChars, 2);
			int32_t value = hexToIntFixedLength(&hexChars[2], 8);
			if (column < numColumns_) {
				locks.set(columnParams_[column], value); // Drops unknown params, and any locks past the maximum
			}
		}

		Note* note = notes.getElement(notes.search(pos, GREATER_OR_EQUAL));
		if (note != nullptr && note->pos == pos) {
			note->lockSet = pool.intern(locks).value_or(0); // Out of memory: the note just has no locks
		}
	}
}

} // namespace deluge::note_locks
