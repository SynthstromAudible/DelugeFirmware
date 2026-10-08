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

#include "dsp-next/core/types.h"
#include "dsp-next/fixed/noise/congruential_step.h"
#include "util/waves.h"

#include <cstdint>

/// @file
/// @brief The synth's noise stream as injectable components: the shared stream and a private one.

namespace deluge::dsp::fixed {

/// @brief The synth's one shared noise stream: the only shared state dsp-next touches.
/// @details Every consumer (the app's CONG sites and any component holding a SharedNoise) draws from the single
/// jcong state in call order, which is what keeps the output bit-exact against the legacy draws.
class SharedNoise {
public:
	using value_type = q31; ///< q31 noise samples.

	/// @brief Advances the shared state exactly as CONG does.
	/// @return The new state, reinterpreted as a q31 sample.
	[[nodiscard, gnu::always_inline]] q31 render() { return static_cast<q31>(CONG); }
};

/// @brief A private noise stream with the same step as the shared one, for specs and components that want their own.
class SeededNoise {
public:
	using value_type = q31; ///< q31 noise samples.

	/// @brief Starts the stream at a state.
	/// @param seed The initial state; the first sample is the step applied to it.
	constexpr explicit SeededNoise(uint32_t seed) : state_(seed) {}

	/// @brief Advances the stream.
	/// @return The new state, reinterpreted as a q31 sample.
	[[nodiscard]] constexpr q31 render() {
		state_ = CongruentialStep::next(state_);
		return static_cast<q31>(state_);
	}

	/// @brief The current state.
	[[nodiscard]] constexpr uint32_t state() const { return state_; }

private:
	uint32_t state_; ///< The generator state.
};

} // namespace deluge::dsp::fixed
