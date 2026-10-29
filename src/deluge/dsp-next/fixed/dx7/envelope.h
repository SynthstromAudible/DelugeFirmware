/*
 * Derived from Dexed's env, under its licence:
 *
 * Copyright 2017 Pascal Gauthier.
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

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief The DX7 operator envelope: four stages of level in log steps, as one control kernel.
namespace deluge::dsp::fixed::dx7 {

/// @brief The DX7 operator envelope: a level in Q24 doublings that moves through four stages at the patch's rates to
/// the patch's levels, offset by the voice's output level.
/// @details Stages 0 to 2 run while the key is down and hold at stage 3's start; release runs stage 3. A stage reads
/// its rate and level when it enters, so a stage under way keeps the target and increment it entered with. Rising,
/// the level first jumps to kJumpTarget, then climbs by a slope that flattens as it nears the top (the DX7's
/// exponential attack); falling, it drops linearly. A stage whose target equals the level, or an attack to level 0,
/// waits a measured number of samples instead (the hold) and then enters the next stage.
///
/// The patch is edited live, so the owner configures from the patch bytes before each block and each event; the
/// output level and rate scaling change only at note-on and retune.
///
/// **A block.** The level moves by the whole block at once; a stage whose target is reached or passed lands on the
/// target and enters the next stage, and the rest of the block is dropped.
struct Envelope {
	using value_type = q31; ///< The level, Q24 doublings.
	using step_type = q31;  ///< A control kernel steps a scalar: the block is its step.

	/// @brief The operator's envelope bytes and the voice's scaling of them.
	struct Settings {
		EnvelopeParams params{}; ///< The op's rates and levels, read live.
		/// The voice's scaled output level, in microsteps (1/32 of a step on the 0 to 127 output scale, about
		/// 0.023 dB): 127 × 32 at full level, plus the velocity's up to 224.
		int32_t output_level = 0;
		int32_t rate_scaling = 0; ///< Added to every stage's rate, in qRate units, 0 to 63.
		int32_t extra_rate = 0;   ///< Added to a stage's rate when a block enters it; 0 for the events.
	};
	/// @brief The envelope reads its settings as they are.
	using Coefficients = Settings;

	/// @brief The envelope between blocks.
	struct State {
		int32_t level = 0;     ///< The level, Q24 doublings: 2^24 is one doubling.
		int32_t target = 0;    ///< The stage's target level, Q24 doublings.
		int32_t stage = 0;     ///< The stage, 0 to 3; 4 once release has finished.
		int32_t increment = 0; ///< The stage's step per sample, 2^-16 of a Q24 doubling (rising: times the slope).
		int32_t hold = 0;      ///< Samples left before a held stage enters the next; 0 when not held.
		bool down = false;     ///< The key is held.
		bool rising = false;   ///< The stage moves up to its target.
	};

	/// @brief Output levels 0 to 19 on the 0 to 127 scale; from 20 the scale is the level plus 28.
	static constexpr std::array<int32_t, 20> kLevelLut{0,  5,  9,  13, 17, 20, 23, 25, 27, 29,
	                                                   31, 33, 35, 37, 39, 41, 42, 43, 45, 46};

	/// @brief A held stage's length in samples at 44.1 kHz, for rates 0 to 76, measured on a DX7 (TF1); from 77 a
	/// hold is 20 × (99 − rate).
	static constexpr std::array<int32_t, 77> kStatics{
	    1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600, 584325, 507150, 502740,
	    441000,  418950,  352800,  308700,  286650,  253575,  220500, 220500, 176400, 145530, 145530, 125685, 110250,
	    110250,  88200,   88200,   74970,   61740,   61740,   55125,  48510,  44100,  37485,  31311,  30870,  27562,
	    27562,   22050,   18522,   17640,   15435,   14112,   13230,  11025,  9261,   9261,   7717,   6615,   6615,
	    5512,    5512,    4410,    3969,    3969,    3439,    2866,   2690,   2249,   1984,   1896,   1808,   1411,
	    1367,    1234,    1146,    926,     837,     837,     705,    573,    573,    529,    441,    441};

	/// @brief The level a rising stage starts from, in 1/65536 of a Q24 doubling: below it the level jumps to it.
	static constexpr int32_t kJumpTarget = 1716;
	/// @brief The microsteps taken off a stage's level plus output level: the DX7's level zero.
	static constexpr int32_t kLevelOffset = 4256;

	/// @brief The settings as they are.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) { return s; }

	/// @brief Moves the envelope on by a block of @p n samples, entering a stage with the configured extra rate.
	/// @param n The block's length in samples; may be 0.
	/// @return The level after the block, Q24 doublings.
	[[nodiscard]] static q31 advance(const Coefficients& c, State& s, std::size_t n);

	/// @brief Key down: the level to 0 and stage 0 entered.
	[[gnu::always_inline]] static void start(const Coefficients& c, State& s) {
		s.level = 0;
		s.down = true;
		enter_stage(c, s, 0, 0);
	}

	/// @brief Key up: stage 3 entered from where the level stands. A second release changes nothing.
	[[gnu::always_inline]] static void release(const Coefficients& c, State& s) {
		if (s.down) {
			s.down = false;
			enter_stage(c, s, 3, 0);
		}
	}

	/// @brief A held key's output level or rate scaling changed: stage 2 entered again with the configured settings.
	/// @details Quirk carried for bit-exactness: the target is first set to stage 2's level without the output
	/// level, and the stage entry then sets it again with the output level, so the first value is never read.
	[[gnu::always_inline]] static void retune(const Coefficients& c, State& s) {
		if (s.down) {
			s.target = stage_target(c.params.levels[2], 0);
			enter_stage(c, s, 2, 0);
		}
	}

	/// @brief An output level byte (0 to 99) on the DX7's 0 to 127 scale.
	[[nodiscard]] [[gnu::always_inline]] static constexpr int32_t scale_output_level(int32_t level) {
		return level >= 20 ? 28 + level : kLevelLut[level];
	}

private:
	/// @brief The target for a stage level byte @p level at @p output_level microsteps: no lower than 16 microsteps.
	/// @details The microsteps are at most 63 × 64 plus the output level, under 2^15, so shifting them into the level's
	/// 2^16 units stays inside the word.
	[[nodiscard]] static int32_t stage_target(int32_t level, int32_t output_level);

	/// @brief Enters @p stage: its target, direction, hold and increment from its level and rate plus @p extra_rate.
	/// Past stage 3 nothing is read and the level holds.
	static void enter_stage(const Coefficients& c, State& s, int32_t stage, int32_t extra_rate);
};

static_assert(control_kernel<Envelope>);
static_assert(!std::is_polymorphic_v<Envelope>);

} // namespace deluge::dsp::fixed::dx7
