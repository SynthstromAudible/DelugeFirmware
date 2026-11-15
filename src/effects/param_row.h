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

/// @file
/// @brief An owner's table row of an effect param: the owner's id, default and scope; the effect's name, mapping and
///        flags.

#include "effects/param_fragment.h"
#include "plugin-api/types/param.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace deluge::effects {

/// @brief What an owner's row of an effect param sets for itself.
struct RowOverrides {
	/// The word a new instance starts with.
	int32_t default_value{0};
	/// Per sound, or per voice for an instrument's patched rows.
	plugin::ParamScope scope{plugin::ParamScope::PER_SOUND};
	/// Flags the owner adds to the effect's (an instrument's smoothing).
	plugin::ParamFlags extra_flags{}; // NOLINT(readability-redundant-member-init): an owner may leave them out
};

/// @brief The owner's row of @p param at @p id.
/// @param id    The owner's id for the param.
/// @param param The effect's param.
/// @param owner The owner's default, scope and added flags.
/// @return The row.
[[nodiscard]] constexpr plugin::ParamInfo row(plugin::ParamId id, const EffectParam& param,
                                              const RowOverrides& owner = {}) {
	return {.id = id,
	        .file_name = param.name,
	        .scope = owner.scope,
	        .default_value = owner.default_value,
	        .mapping = param.mapping,
	        .flags = param.flags | owner.extra_flags};
}

/// @brief Whether each of @p ids names, in @p table, a row of the effect param at the same position of @p fragments:
///        the same name and mapping, and every flag the effect param has.
/// @param table     The owner's table.
/// @param ids       The owner's ids for the effect's params, in the effect's order.
/// @param fragments The effect's params, in its order.
/// @return True when every id's row is its effect param's.
[[nodiscard]] constexpr bool built_from(std::span<const plugin::ParamInfo> table, std::span<const plugin::ParamId> ids,
                                        std::span<const EffectParam> fragments) {
	if (ids.size() != fragments.size()) {
		return false;
	}
	for (std::size_t i = 0; i < ids.size(); ++i) {
		const plugin::ParamInfo* found = plugin::find_param(table, ids[i]);
		if (found == nullptr || found->file_name != fragments[i].name || found->mapping != fragments[i].mapping
		    || (found->flags.bits & fragments[i].flags.bits) != fragments[i].flags.bits) {
			return false;
		}
	}
	return true;
}

} // namespace deluge::effects
