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

#include "plugin-api/types/param.h"
#include "plugin-api/types/plugin_id.h"

#include <optional>
#include <span>
#include <string_view>

namespace deluge::plugin {

/// @brief A param as a file names it where it points at one (a knob's target): the param's file name and, for a
/// cable's depth, the source of the cable nearest the param and the source of the cable controlling that depth.
/// An empty view is absent.
struct ParamRef {
	std::string_view param;
	std::string_view source;
	std::string_view second_source;
	[[nodiscard]] constexpr bool operator==(const ParamRef&) const = default;
};

/// @brief How a plugin names its params in files: its id, a file name → id map, the inverse, and the rule a mod
/// knob's target follows (nullptr: none). The views a ref holds point into the plugin's static names.
struct ParamNaming {
	PluginId plugin;
	std::optional<ParamId> (*id_of)(const ParamRef& ref);
	ParamRef (*ref_of)(ParamId id);
	ParamId (*mod_knob_target)(ParamId id);
};

/// @brief The naming of @p plugin in @p namings, or nullptr.
/// @param namings The available namings.
/// @param plugin The plugin to look up.
/// @return The matching naming, or nullptr.
[[nodiscard]] constexpr const ParamNaming* naming_for(std::span<const ParamNaming> namings, PluginId plugin) {
	for (const ParamNaming& naming : namings) {
		if (naming.plugin == plugin) {
			return &naming;
		}
	}
	return nullptr;
}

} // namespace deluge::plugin
