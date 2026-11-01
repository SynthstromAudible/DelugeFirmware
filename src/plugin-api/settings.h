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

#include "plugin-api/types/setting.h"
#include "serialization/tree.h"
#include "serialization/value.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace deluge::plugin {

/// @brief Whether @p value is one @p table's row for @p path allows: a choice's name, an integer in the row's range,
/// 0 or 1 for a bool, or any text for a text row. False for a structured row or a path the table does not hold.
/// @param table The plugin's setting table.
/// @param path The setting's slash path.
/// @param value The candidate value; only a text value can match.
/// @return Whether @p value is allowed.
[[nodiscard]] inline bool check_setting(std::span<const SettingInfo> table, std::string_view path,
                                        const serialization::Value& value) {
	const std::string* text = serialization::text_of(&value);
	return text != nullptr && check_setting_text(table, path, *text);
}

} // namespace deluge::plugin
