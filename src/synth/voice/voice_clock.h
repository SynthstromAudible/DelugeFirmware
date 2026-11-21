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

#include <cstdint>

namespace deluge::synth {

/// @brief The voice-state clock the cull order reads: each envelope stage a voice enters takes the next tick, so of two
/// voices in one stage the one that entered it first is culled first.
struct VoiceClock {
	uint32_t* next; ///< The next tick to hand out, shared by every voice.

	/// @return The next tick, advancing the clock.
	[[nodiscard]] uint32_t tick() const { return (*next)++; }
};

} // namespace deluge::synth
