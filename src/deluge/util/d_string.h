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
#include "util/exceptions.h"
#include "util/string.h"
#include "util/try.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
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
	String() = default;
	// String(String* otherString); // BEWARE - using this on stack instances sometimes just caused crashes and stuff.
	// Made no sense. Instead, constructing then calling set() works
	~String();
	void clear(bool destructing = false);
	Error set(char const* newChars, int32_t newLength = -1);
	void set(String const* otherString);
	void beenCloned();
	size_t getLength();
	Error shorten(int32_t newLength);
	Error concatenateAtPos(char const* newChars, int32_t pos, int32_t newCharsLength = -1);
	Error concatenateInt(int32_t number, int32_t minNumDigits = 1);
	Error setInt(int32_t number, int32_t minNumDigits = 1);
	Error setChar(char newChar, int32_t pos);
	Error concatenate(String* otherString);
	Error concatenate(char const* newChars);
	Error concatenate(const std::string_view& otherString);
	bool equals(char const* otherChars);
	bool equalsCaseIrrespective(char const* otherChars, int32_t numChars = -1);

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

	inline bool equalsCaseIrrespective(const std::string_view& otherString) {
		return equalsCaseIrrespective(otherString.data(), otherString.size());
	}

	inline bool equalsCaseIrrespective(const String* otherString) {
		if (stringMemory == otherString->stringMemory) {
			return true; // Works if both lengths are 0, too
		}
		if (!stringMemory || !otherString->stringMemory) {
			return false; // If just one is empty, then not equal
		}
		return equalsCaseIrrespective(otherString->get());
	}

	inline char const* get() const {
		if (!stringMemory) {
			return &nothing;
		}
		return stringMemory;
	}

	inline bool isEmpty() { return !stringMemory; }

private:
	int32_t getNumReasons();
	void setNumReasons(int32_t newNum);

	char* stringMemory = nullptr;
};

/// A string buffer with utility functions to append and format contents.
/// does not handle allocation
class StringBuf {
	// Not templated to optimize binary size.
public:
	constexpr StringBuf(char* buf, size_t capacity) : capacity_(capacity - 1), buf_(buf) {
		std::fill_n(buf_, capacity, '\0');
	}

	constexpr void append(std::string_view str, size_t n) {
		size_t to_concat = std::min({n, str.length(), capacity_ - size()});
		if (to_concat > 0) {
			char* ptr = std::copy_n(str.begin(), to_concat, this->end());
			*ptr = '\0';
		}
	}

	constexpr void append(std::string_view str) { append(str, str.length()); }

	constexpr void append(char c) {
		if (length() != capacity()) {
			char* terminal = end();
			terminal[0] = c;
			terminal[1] = '\0';
		}
	}

	constexpr void clear() { std::fill_n(buf_, capacity_ + 1, '\0'); }

	constexpr void truncate(size_t size) {
		if (size <= capacity_) {
			buf_[size] = '\0';
		}
	}

	StringBuf& appendInt(int32_t value, size_t min_digits = 1);

	StringBuf& appendHex(int32_t value, size_t min_digits = 1) {
		intToHex(value, end(), min_digits);
		return *this;
	}

	StringBuf& appendFloat(float value, int32_t precision) {
		char* ptr = D_TRY_CATCH(deluge::to_chars(end(), true_end(), value, precision), error, {
			return *this; // fail if error
		});
		*ptr = '\0';
		return *this;
	}

	[[nodiscard]] constexpr char* data() { return buf_; }
	[[nodiscard]] constexpr const char* data() const { return buf_; }
	[[nodiscard]] constexpr const char* c_str() const { return buf_; }

	[[nodiscard]] constexpr size_t capacity() const { return capacity_; }
	[[nodiscard]] constexpr size_t length() const {
		size_t idx = 0;
		for (; idx <= capacity_; ++idx) {
			if (buf_[idx] == '\0') {
				break;
			}
		}
		return idx;
	}
	[[nodiscard]] constexpr size_t size() const { return length(); }
	[[nodiscard]] constexpr bool full() const { return size() == capacity_; }

	[[nodiscard]] char* begin() { return buf_; }
	[[nodiscard]] char* end() { return &buf_[size()]; }
	[[nodiscard]] const char* cbegin() const { return buf_; }
	[[nodiscard]] const char* cend() const { return &buf_[size()]; }

	[[nodiscard]] bool empty() const { return buf_[0] == '\0'; }

	constexpr bool operator==(const char* rhs) const {
		return static_cast<std::string_view>(*this) == std::string_view{rhs};
	}

	constexpr bool operator==(const std::string_view rhs) const { return static_cast<std::string_view>(*this) == rhs; }

	constexpr bool operator==(const StringBuf& rhs) const {
		return static_cast<std::string_view>(*this) == static_cast<std::string_view>(rhs);
	}

	constexpr void operator+=(const char* cstr) { append(cstr); }
	constexpr void operator+=(std::string_view str) { append(str); }
	constexpr void operator+=(std::string& str) { append(str); }

	operator std::string_view() const { return std::string_view{buf_}; }
	char& operator[](size_t idx) { return buf_[idx]; }

private:
	[[nodiscard]] constexpr char* true_end() const { return &buf_[capacity_ + 1]; }
	size_t capacity_;
	char* buf_;
};

/// Define a `StringBuf` that uses an array placed on the stack.
#define DEF_STACK_STRING_BUF(name, capacity)                                                                           \
	char name##__buf[capacity] = {0};                                                                                  \
	StringBuf name = {name##__buf, capacity}
