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

#include "deluge-project/setting_group.h"
#include "engine-api/types/engine_id.h"
#include "engine-api/types/param.h"
#include "engines/effects/words.h"
#include "gui/l10n/strings.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace deluge::l10n {

/// @brief What a param's name depends on: the filter modes (legacy names the morph param "LPF Drive" or "HPF FM"
/// under a ladder filter).
struct ParamNameContext {
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

/// @brief Whose table a setting row is in: an engine's, or one of the model's setting groups. Two owners can have a
/// row of one path (the audio clip's and the instrument's `transpose`) that a menu shows differently.
class NamesOwner {
public:
	/// @brief How many indices the owners take: one per setting group, then the engines with names, then any other
	/// engine.
	static constexpr uint8_t kCount = std::to_underlying(project::SettingGroupId::LANE_ARP) + 4;

	constexpr NamesOwner(engine::EngineId owner)
	    : index_{owner == engine::kDelugeInstrument   ? kDelugeInstrumentIndex
	             : owner == engine::kGlobalEffectable ? kGlobalEffectableIndex
	                                                  : kOtherEngineIndex} {}
	constexpr NamesOwner(project::SettingGroupId owner) : index_{std::to_underlying(owner)} {}

	/// @brief The owner as a byte: a setting group's value, then the Deluge instrument, the global effectable and any
	/// other engine, below kCount.
	[[nodiscard]] constexpr uint8_t index() const { return index_; }
	/// @brief The setting group, or nullopt for an engine.
	[[nodiscard]] constexpr std::optional<project::SettingGroupId> group() const {
		return index_ < kDelugeInstrumentIndex ? std::optional{static_cast<project::SettingGroupId>(index_)}
		                                       : std::nullopt;
	}
	[[nodiscard]] constexpr bool operator==(const NamesOwner&) const = default;

private:
	static constexpr uint8_t kDelugeInstrumentIndex = std::to_underlying(project::SettingGroupId::LANE_ARP) + 1;
	static constexpr uint8_t kGlobalEffectableIndex = kDelugeInstrumentIndex + 1;
	static constexpr uint8_t kOtherEngineIndex = kGlobalEffectableIndex + 1;
	static_assert(kOtherEngineIndex + 1 == kCount);

	uint8_t index_;
};

/// @brief The names of setting @p path of @p owner's table, as the menu shows the row.
///
/// The engine owners are the Deluge instrument (its settings, its effects and the arp rows) and the global effectable
/// (the effects); the model's owners are its setting groups.
///
/// @param owner The engine or setting group whose table has the row.
/// @param path  The row's path.
/// @return The names, or nullptr where no menu shows the row.
[[nodiscard]] const SettingNames* setting_names(NamesOwner owner, std::string_view path);

/// @brief The display name of param @p id of @p engine.
///
/// @param engine  The engine whose table the id is from (the Deluge instrument or the global effectable).
/// @param id      The param's id in that engine's table.
/// @param state   The filter modes, where the name depends on them.
/// @return The string naming the param; String::STRING_FOR_NONE for an id the engine's table has no param at, or an
///         engine with no param names.
[[nodiscard]] String param_name(engine::EngineId engine, engine::ParamId id, const ParamNameContext& state = {});

} // namespace deluge::l10n
