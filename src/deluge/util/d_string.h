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
#include <memory>
#include <string_view>

extern "C" {
#include "util/cfunctions.h"
}

[[gnu::always_inline]] static inline void intToString(int32_t number, char* buffer) {
	intToString(number, buffer, 1);
}

bool memIsNumericChars(char const* mem, int32_t size);
bool stringIsNumericChars(char const* str);

char halfByteToHexChar(uint8_t thisHalfByte);
void intToHex(uint32_t number, char* output, int32_t numChars = 8);
uint32_t hexToInt(char const* string);
uint32_t hexToIntFixedLength(char const* __restrict__ hexChars, int32_t length);

void byteToHex(uint8_t number, char* buffer);
uint8_t hexToByte(char const* firstChar);

extern const char nothing;

class String {
public:
	String() : stringMemory(std::make_shared<std::string>()) {};

	void clear() { unique().clear(); }

	Error set(std::string_view otherString);
	void set(String const* otherString);
	size_t getLength() const;
	Error shorten(int32_t newLength);
	Error concatenateAtPos(char const* newChars, int32_t pos, int32_t newCharsLength = -1);
	Error concatenateInt(int32_t number, int32_t minNumDigits = 1);
	Error setInt(int32_t number, int32_t minNumDigits = 1);
	Error setChar(char newChar, int32_t pos);
	Error concatenate(String* otherString);
	Error concatenate(char const* newChars);
	bool equals(char const* otherChars) const;
	bool equalsCaseIrrespective(char const* otherChars) const;

	bool contains(const char* otherChars) const { return stringMemory->contains(otherChars); }
	bool equals(String* otherString) const {
		if (stringMemory == otherString->stringMemory) {
			return true; // Works if both lengths are 0, too
		}
		if (!stringMemory || !otherString->stringMemory) {
			return false; // If just one is empty, then not equal
		}
		return equals(otherString->get());
	}

	bool equalsCaseIrrespective(String* otherString) const {
		if (stringMemory == otherString->stringMemory) {
			return true; // Works if both lengths are 0, too
		}
		if (!stringMemory || !otherString->stringMemory) {
			return false; // If just one is empty, then not equal
		}
		return equalsCaseIrrespective(otherString->get());
	}

	char const* get() const {
		if (!stringMemory) {
			return &nothing;
		}
		return stringMemory->c_str();
	}

	bool isEmpty() const { return stringMemory->empty(); }

private:
	std::string& unique() noexcept(false) {
		// If any additional reasons, we gotta clone the memory first
		if (stringMemory.use_count() > 1) {
			stringMemory = std::make_shared<std::string>(*stringMemory);
		}
		return *stringMemory;
	}

	std::shared_ptr<std::string> stringMemory;
};

/// A string buffer with utility functions to append and format contents.
/// does not handle allocation
class StringBuf {
	// Not templated to optimize binary size.
public:
	StringBuf(char* buf, size_t capacity) : capacity_(capacity), buf_(buf) { memset(buf_, '\0', capacity_); }

	void append(const char* str) { ::strncat(buf_, str, capacity_ - size() - 1); }
	void append(char c) { ::strncat(buf_, &c, 1); }
	void clear() { buf_[0] = 0; }
	void truncate(size_t newSize) {
		if (newSize < capacity_) {
			buf_[newSize] = '\0';
		}
	}

	// TODO: Validate buffer size. These can overflow
	void appendInt(int i, int minChars = 1) { intToString(i, buf_ + size(), minChars); }
	void appendHex(int i, int minChars = 1) { intToHex(i, buf_ + size(), minChars); }
	void appendFloat(float f, int32_t minDecimals, int32_t maxDecimals) {
		floatToString(f, buf_ + size(), minDecimals, maxDecimals);
	}

	[[nodiscard]] char* data() { return buf_; }
	[[nodiscard]] const char* data() const { return buf_; }
	[[nodiscard]] const char* c_str() const { return buf_; }

	[[nodiscard]] size_t capacity() const { return capacity_; }
	[[nodiscard]] size_t size() const { return std::strlen(buf_); }
	[[nodiscard]] size_t length() const { return std::strlen(buf_); }

	[[nodiscard]] bool empty() const { return buf_[0] == '\0'; }

	bool operator==(const char* rhs) const { return std::strcmp(buf_, rhs) == 0; }
	bool operator==(const StringBuf& rhs) const { return std::strcmp(buf_, rhs.c_str()) == 0; }

	operator std::string_view() const { return std::string_view{buf_}; }

private:
	size_t capacity_;
	char* buf_;
};

/// Define a `StringBuf` that uses an array placed on the stack.
#define DEF_STACK_STRING_BUF(name, capacity)                                                                           \
	char name##__buf[capacity] = {0};                                                                                  \
	StringBuf name = {name##__buf, capacity}
