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

#include <cstdint>

extern const char nothing;

class String {
public:
	String();
	//String(String* otherString); // BEWARE - using this on stack instances sometimes just caused crashes and stuff. Made no sense. Instead, constructing then calling set() works
	~String();
	void clear(bool destructing = false);
	int32_t set(char const* newChars, int32_t newLength = -1);
	void set(String* otherString);
	void beenCloned();
	int32_t getLength();
	int32_t shorten(int32_t newLength);
	int32_t concatenateAtPos(char const* newChars, int32_t pos, int32_t newCharsLength = -1);
	int32_t concatenateInt(int32_t number, int32_t minNumDigits = 1);
	int32_t setInt(int32_t number, int32_t minNumDigits = 1);
	int32_t setChar(char newChar, int32_t pos);
	int32_t concatenate(String* otherString);
	int32_t concatenate(char const* newChars);
	bool equals(char const* otherChars);
	bool equalsCaseIrrespective(char const* otherChars);

	inline bool equals(String* otherString) {
		if (stringMemory == otherString->stringMemory)
			return true; // Works if both lengths are 0, too
		if (!stringMemory || !otherString->stringMemory)
			return false; // If just one is empty, then not equal
		return equals(otherString->get());
	}

	inline bool equalsCaseIrrespective(String* otherString) {
		if (stringMemory == otherString->stringMemory)
			return true; // Works if both lengths are 0, too
		if (!stringMemory || !otherString->stringMemory)
			return false; // If just one is empty, then not equal
		return equalsCaseIrrespective(otherString->get());
	}

	inline char const* get() {
		if (!stringMemory)
			return &nothing;
		else
			return stringMemory;
	}

	inline bool isEmpty() { return !stringMemory; }

private:
	int32_t getNumReasons();
	void setNumReasons(int32_t newNum);

	char* stringMemory;
};
