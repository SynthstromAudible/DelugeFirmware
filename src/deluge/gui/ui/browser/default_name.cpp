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

namespace deluge::gui::browser {

namespace {

constexpr size_t kMaxSlotDigits = 3;
constexpr int32_t kMaxNumericSuffix = 9999;

bool exists(FileListView const& files, std::string const& nameWithoutExtension) {
	// The browser lists both extensions (allowedFileExtensionsXML), and saving picks between them via writeJsonFlag, so
	// a name is taken if *either* form is on the card. Probing only .XML would hand back a name that already exists.
	return files.contains((nameWithoutExtension + ".XML").c_str())
	       || files.contains((nameWithoutExtension + ".Json").c_str());
}

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

/// Reads the number a family member carries: the digit run straight after `prefix`, which must then run to the
/// extension or to the end of the name. Returns 0 when `name` is not of that form, and kMaxNumericSuffix when the
/// digits run past what we are willing to count to (so the caller stops rather than wrapping).
///
/// The "digits must reach the extension" rule is what keeps a neighbour like "JAM 2024-01-05.XML" out of the "JAM "
/// family. Read as a member it would say the family had reached 2024, and the next save would land on "JAM 2025".
int32_t numberOfSibling(std::string const& name, std::string_view prefix) {
	if (name.size() <= prefix.size()) {
		return 0;
	}
	for (size_t i = 0; i < prefix.size(); i++) {
		if (upper(name[i]) != upper(prefix[i])) {
			return 0;
		}
	}

	int32_t number = 0;
	size_t pos = prefix.size();
	if (!std::isdigit(static_cast<unsigned char>(name[pos]))) {
		return 0;
	}
	while (pos < name.size() && std::isdigit(static_cast<unsigned char>(name[pos]))) {
		number = number * 10 + (name[pos] - '0');
		if (number > kMaxNumericSuffix) {
			return kMaxNumericSuffix;
		}
		pos++;
	}
	return (pos == name.size() || name[pos] == '.') ? number : 0;
}

} // namespace

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

std::string nextDefaultName(std::string_view currentName, std::string_view slotPrefix, FileListView const& files) {
	char letter = 0;
	std::string stem = slotStem(currentName, slotPrefix, &letter);

	// Slot-form name ("SONG185" / "SONG185A"): advance the letter suffix.
	if (!stem.empty()) {
		char from = (letter == 0) ? 'A' : static_cast<char>(letter + 1);
		for (char c = from; c <= 'Z'; c++) {
			std::string candidate = stem + c;
			if (!exists(files, candidate)) {
				return candidate;
			}
		}
		return std::string{currentName}; // Letters exhausted.
	}

	// Anything else: one past the highest-numbered sibling.
	//
	// This asks for that one name rather than probing "<base> 2", "<base> 3", ... upwards, because contains() only
	// sees a window around the file being saved: by "<base> 11" the "2" has fallen out of it and would be handed
	// back as free. Numbering from the top also leaves gaps alone, so a deleted "<base> 3" is not silently reused.
	char delimiter = kNumericSuffixDelimiter;
	std::string base = numericStem(currentName, &delimiter);
	std::string prefix = base + delimiter;

	// The name we are saving over is itself on the card, so its own number is a floor the answer can never go below -
	// which keeps this right even if the folder scan comes back empty because the card would not answer.
	int32_t highest = std::max(numberOfSibling(files.highestNumberedName(prefix.c_str()), prefix),
	                           numberOfSibling(std::string{currentName}, prefix));
	if (highest >= kMaxNumericSuffix) {
		return std::string{currentName}; // Numbers exhausted - the user will have to name this one.
	}
	// An unnumbered original ("MYTRACK.XML" alone) counts as 1, so the first variation is "MYTRACK 2".
	return prefix + std::to_string(std::max<int32_t>(highest, 1) + 1);
}

} // namespace deluge::gui::browser
