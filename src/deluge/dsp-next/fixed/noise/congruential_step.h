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

/// @file
/// @brief The synth's noise step, defined once for the app's CONG macro and for dsp-next's noise components.

namespace deluge::dsp::fixed {

/// @brief The synth's noise step: x' = 69069·x + 1234567 (uint32, wrapping).
struct CongruentialStep {
	static constexpr uint32_t kMultiplier = 69069;  ///< The linear congruential multiplier.
	static constexpr uint32_t kIncrement = 1234567; ///< The linear congruential increment.

	/// @brief Steps the generator state.
	/// @param x The current state.
	/// @return The next state, which is also the next noise sample (reinterpreted as q31).
	[[nodiscard, gnu::always_inline]] static constexpr uint32_t next(uint32_t x) {
		return kMultiplier * x + kIncrement;
	}
};

} // namespace deluge::dsp::fixed
