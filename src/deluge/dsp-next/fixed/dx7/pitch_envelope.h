/*
 * Derived from Dexed's pitchenv, under its licence:
 *
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
#include "dsp-next/fixed/dx7/envelope_params.h"
#include "dsp-next/fixed/dx7/tables.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief The DX7 pitch envelope: four stages of pitch offset, as one control kernel.
namespace deluge::dsp::fixed::dx7 {

/// @brief The DX7 pitch envelope: a pitch offset in Q24 octaves that moves through four stages at the patch's rates
/// to the patch's levels.
/// @details Stages 0 to 2 run while the key is down and hold at stage 3's start; release runs stage 3. A stage reads
/// its rate and level when it enters, so a stage under way keeps the target it entered with. The patch is edited
/// live, so the owner configures from the patch bytes before each block and before each event.
///
/// **A block.** The level moves by the whole block's samples times the stage's increment at once; a stage whose
/// target is reached or passed lands on the target and enters the next stage, and the rest of the block is dropped.
struct PitchEnvelope {
	using value_type = q31; ///< The pitch offset, Q24 octaves.
	using step_type = q31;  ///< A control kernel steps a scalar: the block is its step.

	/// @brief The patch's pitch envelope bytes (126 to 133).
	struct Settings {
		EnvelopeParams params{}; ///< The four rates and four levels.
	};
	/// @brief The envelope reads its settings as they are.
	using Coefficients = Settings;

	/// @brief The envelope between blocks.
	struct State {
		int32_t level = 0;     ///< The pitch offset, Q24 octaves.
		int32_t target = 0;    ///< The stage's target level, Q24 octaves.
		int32_t stage = 0;     ///< The stage, 0 to 3; 4 once release has finished.
		int32_t increment = 0; ///< The stage's level step per sample, Q24 octaves.
		bool rising = false;   ///< The stage moves up to its target.
		bool down = false;     ///< The key is held.
	};

	/// @brief Q24 octaves per sample per step of the rate table at 44.1 kHz.
	static constexpr int32_t kUnit = static_cast<int32_t>((1 << 24) / (21.3 * kSampleRate) + 0.5);

	/// @brief A rate byte (0 to 99) as a multiple of kUnit.
	static constexpr std::array<uint8_t, 100> kRate{
	    1,   2,   3,   3,   4,   4,   5,   5,   6,   6,   7,   7,   8,   8,   9,   9,   10,  10,  11,  11,
	    12,  12,  13,  13,  14,  14,  15,  16,  16,  17,  18,  18,  19,  20,  21,  22,  23,  24,  25,  26,
	    27,  28,  30,  31,  33,  34,  36,  37,  38,  39,  41,  42,  44,  46,  47,  49,  51,  53,  54,  56,
	    58,  60,  62,  64,  66,  68,  70,  72,  74,  76,  79,  82,  85,  88,  91,  94,  98,  102, 106, 110,
	    115, 120, 125, 130, 135, 141, 147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255};

	/// @brief A level byte (0 to 99) as a pitch offset in 1/32 octave, so a level shifted up by 19 is Q24 octaves.
	static constexpr std::array<int8_t, 100> kLevel{
	    -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35, -33, -32, -31,
	    -30,  -29,  -28,  -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11,
	    -10,  -9,   -8,   -7,  -6,  -5,  -4,  -3,  -2,  -1,  0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
	    10,   11,   12,   13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
	    30,   31,   32,   33,  34,  35,  38,  40,  43,  46,  49,  53,  58,  65,  73,  82,  92,  103, 115, 127};

	/// @brief The settings as they are.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) { return s; }

	/// @brief Moves the envelope on by a block of @p n samples.
	/// @param n The block's length in samples; may be 0.
	/// @return The pitch offset after the block, Q24 octaves.
	[[nodiscard]] [[gnu::always_inline]] static q31 advance(const Coefficients& c, State& s, std::size_t n) {
		const auto count = static_cast<int32_t>(n);
		if (s.stage < 3 || (s.stage < 4 && !s.down)) {
			if (s.rising) {
				s.level += count * s.increment;
				if (s.level >= s.target) {
					s.level = s.target;
					enter_stage(c, s, s.stage + 1);
				}
			}
			else {
				s.level -= count * s.increment;
				if (s.level <= s.target) {
					s.level = s.target;
					enter_stage(c, s, s.stage + 1);
				}
			}
		}
		return s.level;
	}

	/// @brief Key down: the level jumps to level 4 and stage 0 starts.
	[[gnu::always_inline]] static void start(const Coefficients& c, State& s) {
		s.level = kLevel[c.params.levels[3]] << 19;
		s.down = true;
		enter_stage(c, s, 0);
	}

	/// @brief Key up: stage 3 starts from where the level stands. A second release changes nothing.
	[[gnu::always_inline]] static void release(const Coefficients& c, State& s) {
		if (s.down) {
			s.down = false;
			enter_stage(c, s, 3);
		}
	}

private:
	/// @brief Enters @p stage, reading its rate and level; past stage 3 nothing is read and the level holds.
	[[gnu::always_inline]] static void enter_stage(const Coefficients& c, State& s, int32_t stage) {
		s.stage = stage;
		if (stage < 4) {
			s.target = kLevel[c.params.levels[stage]] << 19;
			s.rising = s.target > s.level;
			s.increment = kRate[c.params.rates[stage]] * kUnit;
		}
	}
};

static_assert(control_kernel<PitchEnvelope>);
static_assert(!std::is_polymorphic_v<PitchEnvelope>);

} // namespace deluge::dsp::fixed::dx7
