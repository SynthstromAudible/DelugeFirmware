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

#include "gui/ui/browser/default_name.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace deluge::gui::browser {

namespace {

constexpr size_t kMaxSlotDigits = 3;
constexpr int32_t kMaxNumericSuffix = 9999;

char upper(char c) {
	return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

/// If `name` is "<slotPrefix><1-3 digits>" optionally followed by a single letter, returns the name without that
/// trailing letter (the stem to which suffixes get appended) and reports the letter via `letter` (0 when absent).
/// Returns empty when `name` is not of that form, and always for an empty `slotPrefix` (presets - see the header).
std::string slotStem(std::string_view name, std::string_view slotPrefix, char* letter) {
	*letter = 0;
	if (slotPrefix.empty() || name.size() <= slotPrefix.size()) {
		return {};
	}
	for (size_t i = 0; i < slotPrefix.size(); i++) {
		if (upper(name[i]) != upper(slotPrefix[i])) {
			return {};
		}
	}

	size_t digitsStart = slotPrefix.size();
	size_t pos = digitsStart;
	while (pos < name.size() && pos - digitsStart < kMaxSlotDigits
	       && std::isdigit(static_cast<unsigned char>(name[pos]))) {
		pos++;
	}
	if (pos == digitsStart) {
		return {}; // No digits: just a name that happens to start with "SONG".
	}

	std::string stem{name.substr(0, pos)};

	if (pos == name.size()) {
		return stem; // "SONG185"
	}
	if (pos + 1 == name.size() && std::isalpha(static_cast<unsigned char>(name[pos]))) {
		*letter = upper(name[pos]);
		return stem; // "SONG185A"
	}
	return {}; // Trailing junk: not a slot-form name.
}

/// Splits a numeric-suffixed name: "MYTRACK 3" -> base "MYTRACK", delimiter ' '. Leaves `delimiter` as
/// kNumericSuffixDelimiter and returns the whole name when there is no existing suffix to reuse.
std::string numericStem(std::string_view name, char* delimiter) {
	*delimiter = kNumericSuffixDelimiter;

	for (char candidate : {'_', ' '}) {
		size_t at = name.rfind(candidate);
		if (at == std::string_view::npos || at + 1 >= name.size()) {
			continue;
		}
		bool allDigits = true;
		for (size_t i = at + 1; i < name.size(); i++) {
			if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
				allDigits = false;
				break;
			}
		}
		if (allDigits) {
			*delimiter = candidate;
			return std::string{name.substr(0, at)};
		}
	}

	return std::string{name};
}

bool startsWithCaseless(std::string_view name, std::string_view prefix) {
	if (name.size() < prefix.size()) {
		return false;
	}
	for (size_t i = 0; i < prefix.size(); i++) {
		if (upper(name[i]) != upper(prefix[i])) {
			return false;
		}
	}
	return true;
}

/// The number a "<prefix><digits>" family member carries, or 0 if `name` is not one. The digits must run to the
/// extension or the end of the name: that keeps a neighbour like "JAM 2024-01-05.XML" out of the "JAM " family,
/// where it would read as 2024 and send the next save to "JAM 2025". Saturates at kMaxNumericSuffix.
int32_t numberOfSibling(std::string_view name, std::string_view prefix) {
	if (!startsWithCaseless(name, prefix)) {
		return 0;
	}
	size_t pos = prefix.size();
	if (pos == name.size() || !std::isdigit(static_cast<unsigned char>(name[pos]))) {
		return 0;
	}
	int32_t number = 0;
	while (pos < name.size() && std::isdigit(static_cast<unsigned char>(name[pos]))) {
		number = std::min(number * 10 + (name[pos] - '0'), kMaxNumericSuffix);
		pos++;
	}
	return (pos == name.size() || name[pos] == '.') ? number : 0;
}

/// 0-25 if `name` is "<stem><letter>" (any case), with or without an extension; -1 otherwise.
int32_t letterVariationOf(std::string_view name, std::string_view stem) {
	if (!startsWithCaseless(name, stem) || name.size() == stem.size()) {
		return -1;
	}
	const char letter = upper(name[stem.size()]);
	if (letter < 'A' || letter > 'Z') {
		return -1;
	}
	const size_t after = stem.size() + 1;
	return (after == name.size() || name[after] == '.') ? letter - 'A' : -1;
}

} // namespace

NameFamilySurvey::NameFamilySurvey(std::string_view currentName, std::string_view slotPrefix) {
	char letter = 0;
	letterStem_ = slotStem(currentName, slotPrefix, &letter);
	if (letterStem_.empty()) {
		char delimiter = kNumericSuffixDelimiter;
		numberPrefix_ = numericStem(currentName, &delimiter) + delimiter;
	}
}

void NameFamilySurvey::observe(std::string_view fileName) {
	if (!letterStem_.empty()) {
		const int32_t letter = letterVariationOf(fileName, letterStem_);
		if (letter >= 0) {
			takenLetters_ |= 1U << letter;
		}
	}
	else {
		highestNumber_ = std::max(highestNumber_, numberOfSibling(fileName, numberPrefix_));
	}
}

char const* numberPartOf(char const* name, char const* filePrefix) {
	if (!filePrefix) {
		return nullptr;
	}
	size_t prefixLength = strlen(filePrefix);
	for (size_t i = 0; i < prefixLength; i++) {
		if (upper(name[i]) != upper(filePrefix[i])) {
			return nullptr;
		}
	}

	char const* numberPart = &name[prefixLength];
	// Skip zero-padding, but keep the final digit so "SONG000" still reads as slot 0. See the header for why.
	while (*numberPart == '0' && std::isdigit(static_cast<unsigned char>(numberPart[1]))) {
		numberPart++;
	}
	return numberPart;
}

std::string nextDefaultName(std::string_view currentName, std::string_view slotPrefix, NameFamilySurvey const& family) {
	char letter = 0;
	std::string stem = slotStem(currentName, slotPrefix, &letter);

	// Slot-form name ("SONG185" / "SONG185A"): the first free letter after the one we are on.
	if (!stem.empty()) {
		char from = (letter == 0) ? 'A' : static_cast<char>(letter + 1);
		for (char c = from; c <= 'Z'; c++) {
			if ((family.takenLetters() & (1U << (c - 'A'))) == 0) {
				return stem + c;
			}
		}
		return std::string{currentName}; // Letters exhausted.
	}

	// Anything else: one past the family's highest number.
	char delimiter = kNumericSuffixDelimiter;
	const std::string prefix = numericStem(currentName, &delimiter) + delimiter;
	const int32_t highest = std::max(family.highestNumber(), numberOfSibling(currentName, prefix));
	if (highest >= kMaxNumericSuffix) {
		return std::string{currentName}; // Numbers exhausted - the user will have to name this one.
	}
	// An unnumbered original ("MYTRACK" alone) counts as 1, so its first variation is "MYTRACK 2".
	return prefix + std::to_string(std::max<int32_t>(highest, 1) + 1);
}

std::string nextSlotName(std::string_view lastName, std::string_view thingName) {
	// Scans the digits that follow the prefix to compute the next free slot number. A name
	// shorter than the prefix is read as "no digits" rather than read past its end.
	int32_t freeSlotNumber = 1;
	size_t minNumDigits = 1;
	if (!lastName.empty()) {
		freeSlotNumber = 0;
		minNumDigits = 0;
		for (size_t i = thingName.size(); i < lastName.size() && lastName[i] >= '0' && lastName[i] <= '9'; i++) {
			freeSlotNumber = (freeSlotNumber * 10) + (lastName[i] - '0');
			minNumDigits++;
		}
		freeSlotNumber++;
	}
	std::string digits = std::to_string(freeSlotNumber);
	if (digits.size() < minNumDigits) {
		// deluge::string::fromInt's padding, for a number that is never negative.
		digits.insert(0, minNumDigits - digits.size(), '0');
	}
	return std::string{thingName} + digits;
}

} // namespace deluge::gui::browser
