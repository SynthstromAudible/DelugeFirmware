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
#include "dsp-next/fixed/dx7/lfo_delay.h"
#include "dsp-next/fixed/dx7/lookup.h"
#include "dsp-next/fixed/dx7/tables.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief The DX7 patch's LFO: one shared low-frequency wave per patch, as one control kernel.
namespace deluge::dsp::fixed::dx7 {

/// @brief The DX7 patch's LFO: a Q32 phase stepped by the patch's rate, read through the patch's waveform as a
/// Q24 value from 0 to 1.0.
/// @details The patch is edited live, so the owner configures from the patch bytes before each block. Key sync is
/// the one event: it sets the phase to just under half a cycle, so the next block starts the wave at its middle.
///
/// **Quirks carried for bit-exactness:**
/// - *Waveform 0 (triangle) plays the sine.*
/// - *Waveform 5 (sample and hold) holds the middle,* 1 << 23, as every byte past 4 does.
/// - *The phase wraps* in 32 bits, and a block's step is the delta times the block's length in 32 bits.
struct Lfo {
	using value_type = q31; ///< The wave's value, Q24: 0 to 1 << 24.
	using step_type = q31;  ///< A control kernel steps a scalar: the block is its step.

	/// @brief The patch's LFO bytes and the engine's tables.
	struct Settings {
		uint8_t rate = 0;               ///< Byte 137, 0 to 99.
		uint8_t waveform = 0;           ///< Byte 142: 0 triangle, 1 saw down, 2 saw up, 3 square, 4 sine, 5 S&H.
		const Tables* tables = nullptr; ///< The engine's tables, for the sine.
	};

	/// @brief What `advance` reads: the phase step per sample, the waveform and the tables.
	struct Coefficients {
		uint32_t delta = 0;             ///< The phase step per sample, Q32 cycles.
		uint8_t waveform = 0;           ///< As Settings::waveform.
		const Tables* tables = nullptr; ///< The engine's tables, for the sine.
	};

	/// @brief The LFO between blocks. State{} starts the phase at 0: a new patch's LFO starts at the top of its
	/// cycle.
	struct State {
		uint32_t phase = 0; ///< The phase, Q32 cycles.
	};

	/// @brief The phase step for @p s's rate: the rate scaled to 0 to 255, then by 11 and more above 160.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) {
		int32_t sr = s.rate == 0 ? 1 : (165 * s.rate) >> 6;
		sr *= sr < 160 ? 11 : (11 + ((sr - 160) >> 4));
		return {.delta = static_cast<uint32_t>(LfoDelay::kLfoUnit * sr), .waveform = s.waveform, .tables = s.tables};
	}

	/// @brief Moves the phase on by a block of @p n samples and reads the wave there.
	/// @param n The block's length in samples; may be 0.
	/// @return The wave's value after the block, Q24.
	[[nodiscard]] [[gnu::always_inline]] static q31 advance(const Coefficients& c, State& s, std::size_t n) {
		s.phase += static_cast<uint32_t>(n) * c.delta;
		return wave_value(c, s.phase);
	}

	/// @brief Key sync: the phase to just under half a cycle.
	[[gnu::always_inline]] static void key_sync(State& s) { s.phase = (1U << 31) - 1; }

	/// @brief The wave at @p phase, Q24 from 0 to 1 << 24.
	/// @param phase The phase, Q32 cycles, read as a signed word: the square shifts it arithmetically.
	[[nodiscard]] [[gnu::always_inline]] static q31 wave_value(const Coefficients& c, uint32_t phase) {
		const auto p = static_cast<int32_t>(phase);
		switch (c.waveform == 0 ? 4 : c.waveform) {
		case 1: // saw down
			return static_cast<q31>((~p ^ (1U << 31)) >> 8);
		case 2: // saw up
			return static_cast<q31>((p ^ (1U << 31)) >> 8);
		case 3: // square
			return ((~p) >> 7) & (1 << 24);
		case 4: // sine
			return (1 << 23) + (sin_lookup(*c.tables, p >> 8) >> 1);
		default:
			return 1 << 23;
		}
	}
};

static_assert(control_kernel<Lfo>);
static_assert(!std::is_polymorphic_v<Lfo>);

} // namespace deluge::dsp::fixed::dx7
