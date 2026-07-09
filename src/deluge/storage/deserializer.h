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

#include "definitions_cxx.hpp"

#include <cstdint>
#include <string>

class Deserializer {
public:
	virtual bool prepareToReadTagOrAttributeValueOneCharAtATime() = 0;
	virtual char readNextCharOfTagOrAttributeValue() = 0;
	virtual int32_t getNumCharsRemainingInValueBeforeEndOfCluster() = 0;

	virtual char const* readNextTagOrAttributeName() = 0;
	virtual char const* readTagOrAttributeValue() = 0;
	virtual int32_t readTagOrAttributeValueInt() = 0;
	virtual int32_t readTagOrAttributeValueHex(int32_t errorValue) = 0;
	virtual int readTagOrAttributeValueHexBytes(uint8_t* bytes, int32_t maxLen) = 0;
	virtual Error tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware = false) = 0;

	virtual char const* readNextCharsOfTagOrAttributeValue(int32_t numChars) = 0;
	virtual Error readTagOrAttributeValueString(std::string& string) = 0;
	virtual bool match(char const ch) = 0;
	virtual void exitTag(char const* exitTagName = NULL, bool closeObject = false) = 0;

	virtual void reset() = 0;

	/// Whether long reads may interleave the cooperative audio/SD routine. True for file-backed reads
	/// (firmware default, which must service the SD cluster stream and pump the engine); false for
	/// in-memory readers (e.g. the ValueTreeDeserializer), which have nothing to yield to. The
	/// audio-yield gate (AudioEngine::routineWithClusterLoading) consults the reader currently in use.
	[[nodiscard]] virtual bool shouldCallRoutines() const { return true; }
};
