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
/// @brief Whose param table an owner's ENGINE ids are in.

#include "project/param_address.h"

#include <cstdint>

namespace deluge::project {

/// @brief Whose param table an owner's ENGINE ids are in: a sound's (a synth, a kit row) the Deluge instrument's, a
/// global effectable's (the song, a kit's affect-entire, an audio clip) the host chain's table's.
enum class OwnerPlugin : uint8_t { INSTRUMENT, GLOBAL_EFFECTABLE };

/// @brief A param as the UI names it: whose table its ENGINE id is in, and which param in which space.
struct PluginParam {
	OwnerPlugin plugin{OwnerPlugin::INSTRUMENT};
	ParamTarget target{};
	[[nodiscard]] constexpr bool operator==(const PluginParam&) const = default;
};

} // namespace deluge::project
