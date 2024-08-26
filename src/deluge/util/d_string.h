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

#pragma once

#include "definitions_cxx.hpp"
#include <cstdint>
#include <cstring>

extern "C" {
#include "util/cfunctions.h"
}

[[gnu::always_inline]] static inline void intToString(int32_t number, char* buffer) {
	intToString(number, buffer, 1);
}

bool memIsNumericChars(char const* mem, int32_t size);
bool stringIsNumericChars(char const* str);

void byteToHex(uint8_t number, char* buffer);
uint8_t hexToByte(char const* firstChar);

extern const char nothing;

class String {
public:
	String() = default;
	String(const String& other);
	String& operator=(const String& other);

	// String(String* otherString); // BEWARE - using this on stack instances sometimes just caused crashes and stuff.
	// Made no sense. Instead, constructing then calling set() works
	~String();
	void clear(bool destructing = false);
	Error set(char const* newChars, int32_t newLength = -1);
	void set(String const* otherString);
	void beenCloned() const;
	size_t getLength();
	Error shorten(int32_t newLength);
	Error concatenateAtPos(char const* newChars, int32_t pos, int32_t newCharsLength = -1);
	Error concatenateInt(int32_t number, int32_t minNumDigits = 1);
	Error setInt(int32_t number, int32_t minNumDigits = 1);
	Error setChar(char newChar, int32_t pos);
	Error concatenate(String* otherString);
	Error concatenate(char const* newChars);
	Error get_new_memory(int32_t newCharsLength);
	bool equals(char const* otherChars);
	bool equalsCaseIrrespective(char const* otherChars);

	inline bool contains(const char* otherChars) { return strstr(stringMemory, otherChars) != NULL; }
	inline bool equals(String* otherString) {
		if (stringMemory == otherString->stringMemory) {
			return true; // Works if both lengths are 0, too
		}
		if (!stringMemory || !otherString->stringMemory) {
			return false; // If just one is empty, then not equal
		}
		return equals(otherString->get());
	}

	inline bool equalsCaseIrrespective(String* otherString) {
		if (stringMemory == otherString->stringMemory) {
			return true; // Works if both lengths are 0, too
		}
		if (!stringMemory || !otherString->stringMemory) {
			return false; // If just one is empty, then not equal
		}
		return equalsCaseIrrespective(otherString->get());
	}

	// consider removing the explicit inline since the method is already inlined
	[[nodiscard]] inline char const* get() const {
		if (!stringMemory) {
			return &nothing;
		}
		return stringMemory;
	}

	inline bool isEmpty() { return !stringMemory; }
	int32_t getNumReasons() const;

private:
	void setNumReasons(int32_t newNum) const;

	char* stringMemory = nullptr;
};

#include "d_stringbuf.h"
