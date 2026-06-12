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
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

extern "C" {
#include "util/cfunctions.h"
}

[[gnu::always_inline]] static inline void intToString(int32_t number, char* buffer) {
	intToString(number, buffer, 1);
}

bool memIsNumericChars(char const* mem, int32_t size);
bool stringIsNumericChars(std::string_view str);

void byteToHex(uint8_t number, char* buffer);
uint8_t hexToByte(char const* firstChar);

/// Sentinel returned by String::get() for an empty string. Some legacy call sites compare the result of get()
/// against `&nothing` to detect emptiness, so this must be preserved while migrating to std::string.
extern const char nothing;

/// Transitional adapter: `String` is now a thin wrapper around std::string that keeps the legacy method-based API
/// (get/set/concatenate/isEmpty/...) while delegating storage to std::string. The previous bespoke
/// reference-counted, externally-allocated implementation has been removed; std::string's allocations route through
/// the global operator new override into external RAM, matching the old behaviour. Call sites are being migrated to
/// the native std::string API, after which this class will collapse to a plain `using String = std::string`.
///
/// Inheriting from std::string keeps the object layout identical to std::string (no added data members), which is
/// required by NamedThingVector, which reinterprets a member at a fixed offset as a String*.
class String : public std::string {
public:
	using std::string::string;
	using std::string::operator=;

	String() = default;
	String(const String&) = default;
	String& operator=(const String&) = default;
	String(std::string_view sv) : std::string(sv) {}
	String(const std::string& s) : std::string(s) {}
	String(std::string&& s) : std::string(std::move(s)) {}
	String& operator=(std::string_view sv) {
		assign(sv);
		return *this;
	}

	Error set(char const* newChars, int32_t newLength = -1) {
		if (newLength < 0) {
			assign(newChars);
		}
		else {
			assign(newChars, newLength);
		}
		return Error::NONE;
	}
	Error set(std::string_view newStr) {
		assign(newStr);
		return Error::NONE;
	}
	// Accepts both String* and std::string* (String is-a std::string).
	void set(std::string const* otherString) { assign(*otherString); }

	// No-op: copies are now deep, there is no reference counting to track.
	void beenCloned() const {}

	[[nodiscard]] size_t getLength() const { return size(); }

	Error shorten(int32_t newLength) {
		resize(newLength);
		return Error::NONE;
	}
	Error concatenateAtPos(char const* newChars, int32_t pos, int32_t newCharsLength = -1) {
		resize(pos);
		if (newCharsLength < 0) {
			append(newChars);
		}
		else {
			append(newChars, newCharsLength);
		}
		return Error::NONE;
	}
	Error concatenateInt(int32_t number, int32_t minNumDigits = 1) {
		char buffer[12];
		intToString(number, buffer, minNumDigits);
		append(buffer);
		return Error::NONE;
	}
	Error setInt(int32_t number, int32_t minNumDigits = 1) {
		char buffer[12];
		intToString(number, buffer, minNumDigits);
		assign(buffer);
		return Error::NONE;
	}
	Error setChar(char newChar, int32_t pos) {
		(*this)[pos] = newChar;
		return Error::NONE;
	}
	// Accepts both String* and std::string*.
	Error concatenate(std::string const* otherString) {
		append(*otherString);
		return Error::NONE;
	}
	Error concatenate(std::string_view newChars) {
		append(newChars);
		return Error::NONE;
	}
	Error get_new_memory(int32_t newCharsLength) {
		reserve(newCharsLength);
		return Error::NONE;
	}
	[[nodiscard]] bool equals(char const* otherChars) const { return *this == otherChars; }
	[[nodiscard]] bool equalsCaseIrrespective(char const* otherChars) const {
		return strcasecmp(c_str(), otherChars) == 0;
	}
	[[nodiscard]] bool contains(const char* otherChars) const { return find(otherChars) != npos; }
	[[nodiscard]] bool equals(std::string const* otherString) const {
		return static_cast<const std::string&>(*this) == *otherString;
	}
	[[nodiscard]] bool equalsCaseIrrespective(std::string const* otherString) const {
		return strcasecmp(c_str(), otherString->c_str()) == 0;
	}

	/// Returns a C string. For an empty String this returns `&nothing` (not "") to preserve legacy sentinel checks.
	[[nodiscard]] char const* get() const { return empty() ? &nothing : c_str(); }

	[[nodiscard]] bool isEmpty() const { return empty(); }

	// Hides std::string::clear() to keep the legacy signature; the `destructing` flag is no longer meaningful.
	void clear(bool destructing = false) { std::string::clear(); }
};

#include "d_stringbuf.h"
