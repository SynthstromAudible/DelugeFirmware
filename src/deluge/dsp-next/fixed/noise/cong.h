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

#include "dsp-next/fixed/noise/congruential_step.h"

#include <concepts>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief Cong, the synth's noise generator, and the noise_stream concept a kernel's per-block noise satisfies.

namespace deluge::dsp::fixed {

/// @brief A noise stream: a plain value (copyable as bytes, so a wrapper can hold it in a register for a block and
/// write it back) whose `next()` steps it and returns the draw.
template <typename N>
concept noise_stream = std::default_initializable<N> && std::is_trivially_copyable_v<N> && requires(N n) {
	{ n.next() } -> std::same_as<uint32_t>;
};

/// @brief The synth's noise generator: a linear congruential state stepped by @ref CongruentialStep.
/// @details Holds no global: the app owns the one stream every synth-wide draw takes from and passes it in.
class Cong {
public:
	/// @brief A generator at state 0.
	constexpr Cong() = default;

	/// @brief A generator at @p seed; the first draw is the step applied to it.
	constexpr explicit Cong(uint32_t seed) : state_(seed) {}

	/// @brief Steps the state.
	/// @return The new state, which is the draw (a q31 noise sample when reinterpreted).
	[[nodiscard, gnu::always_inline]] constexpr uint32_t next() {
		state_ = CongruentialStep::next(state_);
		return state_;
	}

	/// @brief The current state.
	[[nodiscard]] constexpr uint32_t state() const { return state_; }

	/// @brief Restarts the generator at @p seed.
	constexpr void seed(uint32_t seed) { state_ = seed; }

private:
	uint32_t state_ = 0; ///< The generator state.
};

static_assert(noise_stream<Cong>);

} // namespace deluge::dsp::fixed
