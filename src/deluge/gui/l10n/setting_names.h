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

#include "engine-api/types/param.h"
#include "engines/effects/words.h"
#include "gui/l10n/strings.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace deluge::l10n {

/// @brief Which engine table a param id is from: the Deluge instrument's and the global effectable's ids overlap
/// (both number their unpatched params from 90), and differ in meaning at 116 alone.
enum class ParamTable : uint8_t {
	INSTRUMENT,
	GLOBAL_EFFECTABLE,
};

/// @brief What a param's name depends on: its table, and the filter modes (legacy names the morph param "LPF Drive" or
/// "HPF FM" under a ladder filter).
struct ParamNameContext {
	ParamTable table{ParamTable::INSTRUMENT};
	effects::FilterMode lpf_mode{effects::FilterMode::TRANSISTOR_24DB};
	effects::FilterMode hpf_mode{effects::FilterMode::HPLADDER};
};

/// @brief The texts a menu shows for one setting row.
struct SettingNames {
	String title;                          ///< the menu item's title; `*` stands for the owner's 1-based number
	String name;                           ///< its name in a list
	std::span<const String> choices;       ///< one per choice of a CHOICE row, in the row's choice order; one per menu
	                                       ///< value of a numeric or bool row a menu lists; empty for any other row
	std::span<const String> short_choices; ///< the short spellings of @c choices; empty = the same as @c choices
};

/// @brief The names of setting @p path: an engine's setting row (the Deluge instrument's, the effects', the arp's and
/// its host rows) or one of the model's setting groups, as the menu shows it.
///
/// A path that two tables share is named for the engine's row.
///
/// @param path The row's path.
/// @return The names, or nullptr where no menu shows the row.
[[nodiscard]] const SettingNames* setting_names(std::string_view path);

/// @brief The display name of param @p id.
///
/// @param id      The param's id in its engine's table.
/// @param state   The table @p id is from, and the filter modes where the name depends on them.
/// @return The string naming the param; String::STRING_FOR_NONE for an id no table has.
[[nodiscard]] String param_name(engine::ParamId id, const ParamNameContext& state = {});

} // namespace deluge::l10n
