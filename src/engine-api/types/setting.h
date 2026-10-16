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

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>

namespace deluge::engine {

/// @brief What a setting holds: one of an enum's names, an integer in a range, a bool, free text (a name), or a
/// structured value only its own Edit changes.
enum class SettingKind : uint8_t { CHOICE, INTEGER, BOOL, STRUCTURED, TEXT };

/// @brief A menu value that stands for a stored value outside the linear mapping: max voices' "off" is user 0, stored
/// 127.
struct SettingSentinel {
	int32_t user{0};   ///< the value a menu shows
	int32_t stored{0}; ///< the value the setting stores
};

/// @brief How a menu's value maps onto the stored one: linearly from [user_min, user_max] onto [stored_min,
/// stored_max], except for the sentinels. All zero, with no sentinels, means the menu shows the stored value itself.
struct UserScale {
	int32_t user_min{0};
	int32_t user_max{0};
	int32_t stored_min{0};
	int32_t stored_max{0};
	std::span<const SettingSentinel> sentinels{};

	/// @brief Whether a menu shows the stored value itself, its sentinels aside (no user range of its own).
	[[nodiscard]] constexpr bool identity() const { return user_min == 0 && user_max == 0; }
};

/// @brief What a setting's menu value counts.
enum class SettingUnits : uint8_t { NONE, SEMITONES, CENTS, MILLISECONDS, SYNC_LEVEL };

/// @brief One non-param setting of an engine or a model record, named by its slash path in the owner's state
/// element, with what a menu needs to show and edit it.
struct SettingInfo {
	std::string_view path;
	SettingKind kind{SettingKind::INTEGER};
	std::span<const std::string_view> choices;   ///< CHOICE: the names a file writes
	int32_t min{0};                              ///< INTEGER: the least stored value
	int32_t max{0};                              ///< INTEGER: the greatest stored value
	UserScale user{};                            ///< INTEGER: how a menu's value maps onto the stored one
	std::span<const uint16_t> display_choices{}; ///< CHOICE: a menu's name for each choice, as the firmware's l10n ids
	int32_t default_value{0};                    ///< the stored value a new owner starts with
	SettingUnits units{SettingUnits::NONE};      ///< what a menu's value counts
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

/// @brief @p text as a whole decimal integer, or nothing.
/// @param text The text to parse; it must be entirely a decimal integer.
/// @return The parsed integer, or std::nullopt if @p text is not wholly one.
[[nodiscard]] inline std::optional<int64_t> whole_number(std::string_view text) {
	int64_t n = 0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), n);
	if (error != std::errc{} || end != text.data() + text.size()) {
		return std::nullopt;
	}
	return n;
}

/// @brief Whether @p text is a value @p table's row for @p path allows: a choice's name, an integer in the row's
/// range, 0 or 1 for a bool, or any text for a text row. False for a structured row or a path the table does not hold.
/// @param table The setting table.
/// @param path  The setting's slash path.
/// @param text  The candidate value, as text.
/// @return Whether @p text is allowed.
[[nodiscard]] inline bool check_setting_text(std::span<const SettingInfo> table, std::string_view path,
                                             std::string_view text) {
	const SettingInfo* row = find_setting(table, path);
	if (row == nullptr) {
		return false;
	}
	switch (row->kind) {
	case SettingKind::CHOICE:
		return std::ranges::find(row->choices, text) != row->choices.end();
	case SettingKind::INTEGER: {
		const std::optional<int64_t> n = whole_number(text);
		return n && *n >= row->min && *n <= row->max;
	}
	case SettingKind::BOOL:
		return text == "0" || text == "1";
	case SettingKind::TEXT:
		return true; // any text; the owner checks what it allows (a duplicate name)
	case SettingKind::STRUCTURED:
		break;
	}
	return false;
}

/// @brief @p stored as a menu shows it.
/// @param row    The setting's row.
/// @param stored A value within the row's stored range.
/// @return The menu's value: a sentinel's user value, the linear mapping's, or @p stored itself with no user scale.
[[nodiscard]] constexpr int32_t to_user(const SettingInfo& row, int32_t stored) {
	for (const SettingSentinel& sentinel : row.user.sentinels) {
		if (sentinel.stored == stored) {
			return sentinel.user;
		}
	}
	const int64_t span_stored = int64_t{row.user.stored_max} - row.user.stored_min;
	if (row.user.identity() || span_stored == 0) {
		return stored;
	}
	const int64_t span_user = int64_t{row.user.user_max} - row.user.user_min;
	const int64_t offset = int64_t{stored} - row.user.stored_min;
	return static_cast<int32_t>(row.user.user_min + (offset * span_user + span_stored / 2) / span_stored);
}

/// @brief A menu's @p user value as the row stores it.
/// @param row  The setting's row.
/// @param user The menu's value.
/// @return The stored value, or nullopt where @p user is outside the row's user range (with no user scale, its stored
///         range).
[[nodiscard]] constexpr std::optional<int32_t> to_stored(const SettingInfo& row, int32_t user) {
	for (const SettingSentinel& sentinel : row.user.sentinels) {
		if (sentinel.user == user) {
			return sentinel.stored;
		}
	}
	if (row.user.identity()) {
		return (user >= row.min && user <= row.max) ? std::optional<int32_t>{user} : std::nullopt;
	}
	if (user < row.user.user_min || user > row.user.user_max) {
		return std::nullopt;
	}
	const int64_t span_user = int64_t{row.user.user_max} - row.user.user_min;
	if (span_user == 0) {
		return row.user.stored_min;
	}
	const int64_t span_stored = int64_t{row.user.stored_max} - row.user.stored_min;
	return static_cast<int32_t>(row.user.stored_min
	                            + (int64_t{user - row.user.user_min} * span_stored + span_user / 2) / span_user);
}

} // namespace deluge::engine
