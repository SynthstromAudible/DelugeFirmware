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

#include "engine-api/types/setting.h"
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

namespace deluge::engine {

/// @brief @p text as a whole decimal integer, or nothing.
[[nodiscard]] inline std::optional<int64_t> whole_number(std::string_view text) {
	int64_t n = 0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), n);
	if (error != std::errc{} || end != text.data() + text.size()) {
		return std::nullopt;
	}
	return n;
}

/// @brief Whether @p value is one @p table's row for @p path allows: a choice's name, an integer in the row's range, or
/// 0 or 1 for a bool. False for a structured row or a path the table does not hold.
[[nodiscard]] inline bool check_setting(std::span<const SettingInfo> table, std::string_view path,
                                        const serialization::Value& value) {
	const SettingInfo* row = find_setting(table, path);
	const std::string* text = serialization::text_of(&value);
	if (row == nullptr || text == nullptr) {
		return false;
	}
	switch (row->kind) {
	case SettingKind::CHOICE:
		return std::ranges::find(row->choices, std::string_view(*text)) != row->choices.end();
	case SettingKind::INTEGER: {
		const std::optional<int64_t> n = whole_number(*text);
		return n && *n >= row->min && *n <= row->max;
	}
	case SettingKind::BOOL:
		return *text == "0" || *text == "1";
	case SettingKind::STRUCTURED:
		break;
	}
	return false;
}

} // namespace deluge::engine
