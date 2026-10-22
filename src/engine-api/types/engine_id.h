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

namespace deluge::engine {

/// @brief An engine's stable name, as a song stores it: a reverse-domain-style string that never changes once used.
struct EngineId {
	std::string_view name;
	[[nodiscard]] constexpr bool operator==(const EngineId&) const = default;
};

/// @brief The Deluge instrument: today's whole sound pipeline as one engine.
inline constexpr EngineId kDelugeInstrument{"deluge.instrument"};

/// @brief The Deluge effects: a host owner's (a kit's, an audio track's, the song master's) effect chain.
inline constexpr EngineId kDelugeEffects{"deluge.effects"};

/// @brief The params the song master, a kit's affect-entire and an audio track share, until each effect declares
/// its own.
inline constexpr EngineId kGlobalEffectable{"deluge.global-effectable"};

} // namespace deluge::engine
