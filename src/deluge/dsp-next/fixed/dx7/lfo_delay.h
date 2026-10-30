/*
 * Derived from Dexed's dx7note, under its licence:
 *
 * Copyright 2016-2017 Pascal Gauthier.
 * Copyright 2012 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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

#include "dsp-next/core/kernel.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/dx7/tables.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief The DX7 LFO delay: the LFO's depth fading in after a note starts, as one control kernel.
namespace deluge::dsp::fixed::dx7 {

/// @brief The DX7 LFO delay: how much of the LFO's depth applies, from 0 while the delay runs to 1.0 once it has.
/// @details A 32-bit position runs from 0. Its first half is the delay, at the delay's increment, and returns 0; its
/// second half is the fade in, at the settled increment, and returns the position's low 31 bits as a Q24 ramp up to
/// just under 1.0. A block that would carry the position past 2^32 returns 1.0 and leaves the position where it was,
/// so every later block does the same.
///
/// **Quirks carried for bit-exactness:**
/// - *The step wraps.* A block's step is the increment times the block's length as a 32-bit product, which wraps
///   before it is added to the position in 64 bits.
/// - *No delay is no fade.* A delay of 0 sets both increments to 2^32 - 1, so the first block of any length but 0
///   passes 2^32 and returns 1.0.
struct LfoDelay {
	using value_type = q31; ///< The depth that applies, Q24: 0 to 1 << 24.
	using step_type = q31;  ///< A control kernel steps a scalar: the block is its step.

	/// @brief The patch's LFO delay byte (138).
	struct Settings {
		uint8_t delay = 0; ///< 0 to 99: 0 is none, 99 the longest.
	};

	/// @brief The position's increments per sample, for the delay and the fade in.
	struct Coefficients {
		uint32_t increment = ~0U;         ///< Through the first half of the position: the delay.
		uint32_t settled_increment = ~0U; ///< Through the second half: the fade in.
	};

	/// @brief The delay between blocks. State{} is a note's start.
	struct State {
		uint32_t position = 0; ///< Where the delay is, 0 to 2^32 - 1: the delay below 2^31, the fade in above.
	};

	/// @brief The LFO's phase units per sample per step of its rate at 44.1 kHz; the LFO reads it from here too.
	static constexpr int32_t kLfoUnit = static_cast<int32_t>(25190424 / static_cast<float>(kSampleRate) + 0.5);

	/// @brief The increments for @p s: the delay's from its byte, the fade's rounded down to a multiple of 0x80
	/// steps and no fewer than 0x80.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) {
		int32_t a = 99 - s.delay;
		if (a == 99) {
			return {};
		}
		a = (16 + (a & 15)) << (1 + (a >> 4));
		const auto increment = static_cast<uint32_t>(kLfoUnit * a);
		a &= 0xff80;
		a = std::max(0x80, a);
		return {.increment = increment, .settled_increment = static_cast<uint32_t>(kLfoUnit * a)};
	}

	/// @brief Moves the delay on by a block of @p n samples.
	/// @param n The block's length in samples; may be 0.
	/// @return The depth that applies after the block, Q24.
	[[nodiscard]] [[gnu::always_inline]] static q31 advance(const Coefficients& c, State& s, std::size_t n) {
		const uint32_t delta = s.position < (1U << 31) ? c.increment : c.settled_increment;
		const uint64_t d = uint64_t{s.position} + (delta * static_cast<uint32_t>(n)); // wraps in 32 bits first
		if (d > ~0U) {
			return 1 << 24;
		}
		s.position = static_cast<uint32_t>(d);
		if (d < (1U << 31)) {
			return 0;
		}
		return static_cast<q31>((d >> 7) & ((1 << 24) - 1));
	}
};

static_assert(control_kernel<LfoDelay>);
static_assert(!std::is_polymorphic_v<LfoDelay>);

} // namespace deluge::dsp::fixed::dx7
