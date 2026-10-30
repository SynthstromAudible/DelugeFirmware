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

#include "effects/global_effectable/params.h"
#include "plugin-api/types/param_naming.h"

namespace deluge::global_effectable {

namespace detail {
inline std::optional<plugin::ParamId> id_of(const plugin::ParamRef& ref) {
	if (!ref.source.empty() || !ref.second_source.empty()) {
		return std::nullopt;
	}
	const plugin::ParamInfo* row = plugin::find_param(kParams, ref.param);
	return row != nullptr ? std::optional<plugin::ParamId>{row->id} : std::nullopt;
}

inline plugin::ParamRef ref_of(plugin::ParamId id) {
	const plugin::ParamInfo* row = plugin::find_param(kParams, id);
	return row != nullptr ? plugin::ParamRef{row->file_name} : plugin::ParamRef{};
}
} // namespace detail

/// @brief How files name the provisional global-effectable params: by file name only (they have no cables) and
/// with no mod-knob rule.
inline constexpr plugin::ParamNaming kNaming{plugin::kGlobalEffectable, &detail::id_of, &detail::ref_of, nullptr};

} // namespace deluge::global_effectable
