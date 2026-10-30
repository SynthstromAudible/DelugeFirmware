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
#include <span>
#include <string_view>

namespace deluge::plugin {

/// @brief What a setting holds: one of an enum's names, an integer in a range, a bool, or a structured value only its
/// own Edit changes.
enum class SettingKind : uint8_t { CHOICE, INTEGER, BOOL, STRUCTURED };

/// @brief One non-param setting of a plugin, named by its slash path in the plugin's state element.
struct SettingInfo {
	std::string_view path;
	SettingKind kind{SettingKind::INTEGER};
	std::span<const std::string_view> choices; ///< CHOICE: the names a file writes
	int32_t min{0};                            ///< INTEGER: the least value
	int32_t max{0};                            ///< INTEGER: the greatest value
};

/// @brief The row of @p table for @p path, or nullptr.
/// @param table The rows to search.
/// @param path  The slash path of the setting.
/// @return The matching row, or `nullptr` when none has that path.
[[nodiscard]] constexpr const SettingInfo* find_setting(std::span<const SettingInfo> table, std::string_view path) {
	for (const SettingInfo& row : table) {
		if (row.path == path) {
			return &row;
		}
	}
	return nullptr;
}

} // namespace deluge::plugin
