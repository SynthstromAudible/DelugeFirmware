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

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/noise/cong.h"

#include <cstdint>
#include <type_traits>

/// @file
/// @brief SeededNoise: a private noise stream as a generator component.

namespace deluge::dsp::fixed {

/// @brief A private noise stream with the synth's step, as a generator: for specs and components that want their own.
class SeededNoise {
public:
	using value_type = q31; ///< q31 noise samples.

	/// @brief Starts the stream at a state.
	/// @param seed The initial state; the first sample is the step applied to it.
	constexpr explicit SeededNoise(uint32_t seed) : cong_(seed) {}

	/// @brief Advances the stream.
	/// @return The new state, reinterpreted as a q31 sample.
	[[nodiscard, gnu::always_inline]] constexpr q31 render() { return static_cast<q31>(cong_.next()); }

	/// @brief The current state.
	[[nodiscard]] constexpr uint32_t state() const { return cong_.state(); }

private:
	Cong cong_; ///< The generator.
};

static_assert(sample_generator<SeededNoise>);
static_assert(!std::is_polymorphic_v<SeededNoise>);

} // namespace deluge::dsp::fixed
