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

} // namespace deluge::storage
