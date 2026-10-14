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

#include "engine-api/types/param_naming.h"
#include "engines/global_effectable/params.h"

namespace deluge::global_effectable {

namespace detail {
inline std::optional<engine::ParamId> id_of(const engine::ParamRef& ref) {
	if (!ref.source.empty() || !ref.second_source.empty()) {
		return std::nullopt;
	}
	const engine::ParamInfo* row = engine::find_param(kParams, ref.param);
	return row != nullptr ? std::optional<engine::ParamId>{row->id} : std::nullopt;
}

inline engine::ParamRef ref_of(engine::ParamId id) {
	const engine::ParamInfo* row = engine::find_param(kParams, id);
	return row != nullptr ? engine::ParamRef{row->file_name} : engine::ParamRef{};
}
} // namespace detail

/// @brief How files name the provisional global-effectable params: by file name only (they have no cables) and
/// with no mod-knob rule.
inline constexpr engine::ParamNaming kNaming{engine::kGlobalEffectable, &detail::id_of, &detail::ref_of, nullptr};

} // namespace deluge::global_effectable
