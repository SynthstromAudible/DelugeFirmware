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

#pragma once

#include <cstdint>
#include <string_view>

namespace deluge::storage {

/// @brief How a document is written: the format its file holds.
enum class DocumentFormat : uint8_t {
	XML,
	JSON,
};

/// @brief The format songs are saved in.
inline constexpr DocumentFormat kSongSaveFormat = DocumentFormat::JSON;

/// @brief The format of the document at @p path: JSON when its extension is `json`, in any case; XML otherwise.
[[nodiscard]] constexpr DocumentFormat formatOfPath(std::string_view path) {
	const size_t dot = path.rfind('.');
	if (dot == std::string_view::npos || path.size() - dot != 5) {
		return DocumentFormat::XML;
	}
	constexpr std::string_view kJson = "json";
	for (size_t i = 0; i < kJson.size(); i++) {
		char c = path[dot + 1 + i];
		if (c >= 'A' && c <= 'Z') {
			c = static_cast<char>(c - 'A' + 'a');
		}
		if (c != kJson[i]) {
			return DocumentFormat::XML;
		}
	}
	return DocumentFormat::JSON;
}

/// @brief The extension a document of @p format is saved with.
[[nodiscard]] constexpr std::string_view extensionOf(DocumentFormat format) {
	return format == DocumentFormat::JSON ? ".json" : ".XML";
}

/// @brief Whether @p text starts with @p prefix, ignoring the case of ASCII letters (as the card's file names do).
[[nodiscard]] constexpr bool caselessPrefix(std::string_view text, std::string_view prefix) {
	if (text.size() < prefix.size()) {
		return false;
	}
	for (size_t i = 0; i < prefix.size(); i++) {
		char a = text[i];
		char b = prefix[i];
		a = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
		b = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
		if (a != b) {
			return false;
		}
	}
	return true;
}

/// @brief Whether @p file is the upgraded copy of @p other: a JSON song of the same name (in any case) as @p other, an
/// XML one -- the copy a save of an XML song writes beside it.
[[nodiscard]] constexpr bool isUpgradeOf(std::string_view file, std::string_view other) {
	if (formatOfPath(file) != DocumentFormat::JSON || formatOfPath(other) != DocumentFormat::XML) {
		return false;
	}
	const size_t fileDot = file.rfind('.');
	const size_t otherDot = other.rfind('.');
	return fileDot == otherDot && other.size() - otherDot == 4 && caselessPrefix(other, file.substr(0, fileDot + 1));
}

/// @brief Whether the file at @p index of a listing sorted by name is an upgraded copy of a file beside it.
/// @param count  How many files the listing holds.
/// @param nameAt Gives the name, extension included, of the file at an index below @p count.
template <class NameAt>
[[nodiscard]] constexpr bool isUpgradedCopy(size_t count, size_t index, NameAt nameAt) {
	const std::string_view file = nameAt(index);
	if (formatOfPath(file) != DocumentFormat::JSON) {
		return false;
	}
	// The XML save sorts beside it, past only other files whose names start with the same name and a dot.
	const std::string_view stem = file.substr(0, file.rfind('.') + 1);
	for (size_t i = index; i-- > 0 && caselessPrefix(nameAt(i), stem);) {
		if (isUpgradeOf(file, nameAt(i))) {
			return true;
		}
	}
	for (size_t i = index + 1; i < count && caselessPrefix(nameAt(i), stem); i++) {
		if (isUpgradeOf(file, nameAt(i))) {
			return true;
		}
	}
	return false;
}

} // namespace deluge::storage
