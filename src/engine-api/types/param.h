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

#include "engine-api/types/setting_units.h"

#include <compare>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace deluge::engine {

/// @brief A param's stable number in its engine's table. An engine never reuses an id; a new param takes a new one.
struct ParamId {
	uint32_t value{0};
	[[nodiscard]] constexpr auto operator<=>(const ParamId&) const = default;
};

/// @brief Whether a param can take a different value in each voice, or has one value for the whole instance.
enum class ParamScope : uint8_t { PER_VOICE, PER_SOUND };

/// @brief The stored words a param can take. Values are signed 32-bit words; unless a row says otherwise a param
/// spans all of them (for the Deluge's params, a knob position in Q31).
struct ParamRange {
	int32_t min{std::numeric_limits<int32_t>::min()};
	int32_t max{std::numeric_limits<int32_t>::max()};
	[[nodiscard]] constexpr bool operator==(const ParamRange&) const = default;
};

/// @brief How a menu shows a param's stored word.
enum class ParamDisplay : uint8_t {
	STANDARD,       ///< the whole word range onto 0..50
	HALF_PRECISION, ///< a word that is never negative (0..INT32_MAX) onto 0..50
	PAN,            ///< the whole word range onto -25..25
};

/// @brief One row of an engine's param table.
struct ParamInfo {
	ParamId id;
	/// The name files give the param where they name it (a cable's destination, a knob's target).
	std::string_view file_name;
	ParamScope scope{ParamScope::PER_SOUND};
	/// Centred on zero (pan, pitch) rather than rising from its minimum.
	bool bipolar{false};
	/// What a menu's value counts, where it shows a unit.
	SettingUnits units{SettingUnits::NONE};
	/// How a menu scales the stored word.
	ParamDisplay display{ParamDisplay::STANDARD};
	/// The stored word a new instance starts with.
	int32_t default_value{0};
	ParamRange range{};
	/// A gold knob's indicator blinks at the param's middle value (a centre that means "none": pan, pitch, an EQ
	/// band, the delay).
	bool indicates_middle{false};
};

/// @brief A dense run of ids an engine stores together: @c count ids from @c first.
struct ParamBlock {
	ParamId first;
	uint32_t count{0};

	/// @brief Whether @p id is one of this block's.
	/// @param id The param id to test.
	/// @return True if @p id lies in [first, first + count).
	[[nodiscard]] constexpr bool contains(ParamId id) const {
		return id.value >= first.value && id.value - first.value < count;
	}
};

/// @brief The row of @p table with id @p id, or nullptr.
/// @param table The engine's param table.
/// @param id The param id to find.
/// @return The matching row, or nullptr.
[[nodiscard]] constexpr const ParamInfo* find_param(std::span<const ParamInfo> table, ParamId id) {
	for (const ParamInfo& row : table) {
		if (row.id == id) {
			return &row;
		}
	}
	return nullptr;
}

/// @brief The row of @p table named @p file_name, or nullptr. An empty name matches no row.
/// @param table The engine's param table.
/// @param file_name The name files give the param.
/// @return The matching row, or nullptr.
[[nodiscard]] constexpr const ParamInfo* find_param(std::span<const ParamInfo> table, std::string_view file_name) {
	if (file_name.empty()) {
		return nullptr;
	}
	for (const ParamInfo& row : table) {
		if (row.file_name == file_name) {
			return &row;
		}
	}
	return nullptr;
}

} // namespace deluge::engine
