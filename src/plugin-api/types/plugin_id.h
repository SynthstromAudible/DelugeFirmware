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

#include <string_view>

namespace deluge::plugin {

/// @brief A plugin's stable name, as a song stores it: a reverse-domain-style string that never changes once used.
struct PluginId {
	std::string_view name;
	[[nodiscard]] constexpr bool operator==(const PluginId&) const = default;
};

/// @brief The Deluge instrument plugin.
inline constexpr PluginId kDelugeInstrument{"deluge.instrument"};

/// @brief The params the song master, a kit's affect-entire and an audio track share, until each effect declares
/// its own.
inline constexpr PluginId kGlobalEffectable{"deluge.global-effectable"};

} // namespace deluge::plugin
