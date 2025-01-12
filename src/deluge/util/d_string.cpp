/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "util/d_string.h"
#include "definitions_cxx.hpp"
#include "memory/general_memory_allocator.h"
#include "util/cfunctions.h"
#include <bit>
#include <cstring>

const char nothing = 0;

Error String::set(std::string_view newChars) {
	if (newChars.empty()) {
		clear();
		return Error::NONE;
	}

	// If we're here, new length is not 0
	if (stringMemory.use_count() > 1) {
		stringMemory = std::make_shared<std::string>(newChars);
	}
	else {
		stringMemory->assign(newChars);
	}
	return Error::NONE;
}

// This one can't fail!
void String::set(String const* otherString) {
	stringMemory = otherString->stringMemory;
}

size_t String::getLength() const {
	return stringMemory->length();
}

Error String::shorten(int32_t newLength) {
	if (newLength == 0) {
		this->clear();
	}
	else {
		unique().resize(newLength);
	}
	return Error::NONE;
}

Error String::concatenate(String* otherString) {
	if (!stringMemory) {
		set(otherString);
		return Error::NONE;
	}

	return concatenate(otherString->get());
}

Error String::concatenate(char const* newChars) {
	return concatenateAtPos(newChars, getLength());
}

Error String::concatenateAtPos(char const* newChars, int32_t pos, int32_t newCharsLength) {
	if (pos == 0) {
		if (newCharsLength < 0) {
			newCharsLength = strlen(newChars);
		}
		return set({newChars, static_cast<size_t>(newCharsLength)});
	}

	unique().resize(pos);

	unique() += newChars;

	return Error::NONE;
}

Error String::concatenateInt(int32_t number, int32_t minNumDigits) {
	char buffer[12];
	intToString(number, buffer, minNumDigits);
	return concatenate(buffer);
}

Error String::setInt(int32_t number, int32_t minNumDigits) {
	char buffer[12];
	intToString(number, buffer, minNumDigits);
	return set(buffer);
}

Error String::setChar(char newChar, int32_t pos) {
	unique().at(pos) = newChar;
	return Error::NONE;
}

bool String::equals(char const* otherChars) const {
	return *stringMemory == otherChars;
}

bool String::equalsCaseIrrespective(char const* otherChars) const {
	return !strcasecmp(get(), otherChars);
}

/**********************************************************************************************************************\
 * String formatting and parsing functions
\**********************************************************************************************************************/

char halfByteToHexChar(uint8_t thisHalfByte) {
	if (thisHalfByte < 10) {
		return 48 + thisHalfByte;
	}
	else {
		return 55 + thisHalfByte;
	}
}

char hexCharToHalfByte(unsigned char hexChar) {
	if (hexChar >= 65) {
		return hexChar - 55;
	}
	else {
		return hexChar - 48;
	}
}

void intToHex(uint32_t number, char* output, int32_t numChars) {
	output[numChars] = 0;
	for (int32_t i = numChars - 1; i >= 0; i--) {
		output[i] = halfByteToHexChar(number & 15);
		number >>= 4;
	}
}

uint32_t hexToInt(char const* string) {
	int32_t output = 0;
	while (*string) {
		output <<= 4;
		output |= hexCharToHalfByte(*string);
		string++;
	}
	return output;
}

// length must be >0
uint32_t hexToIntFixedLength(char const* __restrict__ hexChars, int32_t length) {
	uint32_t output = 0;
	char const* const endChar = hexChars + length;
	do {
		output <<= 4;
		output |= hexCharToHalfByte(*hexChars);
		hexChars++;
	} while (hexChars != endChar);

	return output;
}
