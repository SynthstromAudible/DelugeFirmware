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
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief Click: the metronome's tick, a square added to stereo frames, as a processor kernel.
namespace deluge::dsp::fixed {

/// @brief A square wave at a pitch set by each trigger, added to both channels, sounding for whole blocks until 1000
/// frames have passed since the trigger.
/// @details High (+level) while the phase is at or below 2^31 − 1, low (−level) above; the negation and the sums wrap
/// as the target's `neg` and `add` do. **Aliasing.** The square is not band-limited: an edge, unfiltered, is the
/// click's sound. **The gate.** It is checked between blocks, so a click lasts to the end of the block in which it
/// passes 1000 frames; only a trigger opens it.
///
/// **Events.** @ref trigger starts a click, restarting one under way; @ref sounding says whether the next block
/// renders, and the owner skips the kernel when it does not.
struct Click {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.

	static constexpr uint32_t kGateFrames = 1000;       ///< Frames after which the click stops, at a block's end.
	static constexpr uint32_t kHighUntil = 2147483647u; ///< The phase at and below which the square is high.

	/// @brief The click's level.
	struct Settings {
		q31 level = 0; ///< The amplitude, Q31.
	};
	/// @brief The click's level.
	struct Coefficients {
		q31 level = 0; ///< The amplitude, Q31.
	};
	/// @brief The square's phase and the gate.
	struct State {
		uint32_t phase = 0;         ///< The square's phase.
		uint32_t increment = 0;     ///< Per frame, set by the trigger.
		uint32_t since_trigger = 0; ///< Frames rendered since the trigger.
		bool sounding = false;      ///< Whether the next block renders.
	};

	/// @brief The level, unchanged.
	[[nodiscard]] static constexpr Coefficients coefficients(const Settings& s) { return {.level = s.level}; }

	/// @brief Starts a click at @p increment per frame; a click under way restarts.
	/// @param s The state.
	/// @param increment The phase increment per frame.
	static void trigger(State& s, uint32_t increment) {
		s = {.phase = 0, .increment = increment, .since_trigger = 0, .sounding = true};
	}

	/// @brief Whether the next block renders.
	/// @param s The state.
	/// @return Whether the click is sounding.
	[[nodiscard]] static bool sounding(State& s) { return s.sounding; }

	/// @brief Counts the block's frames, and closes the gate after the block if they pass @ref kGateFrames.
	/// @details The loop never reads the gate, so closing it before the loop is the same as after.
	[[nodiscard]] [[gnu::always_inline]] static EmptyBlock prepare(const Coefficients& /*c*/, State& s, std::size_t n) {
		s.since_trigger += static_cast<uint32_t>(n);
		s.sounding = s.sounding && s.since_trigger <= kGateFrames;
		return {};
	}

	/// @brief One frame with the square added.
	[[nodiscard]] [[gnu::always_inline]] static StereoSample step(const Coefficients& c, const EmptyBlock& /*b*/,
	                                                              State& s, StereoSample x) {
		const q31 value = s.phase <= kHighUntil ? c.level : wrapping_sub(0, c.level);
		s.phase += s.increment;
		return {.l = wrapping_add(x.l, value), .r = wrapping_add(x.r, value)};
	}
};

static_assert(processor_kernel<Click>);
static_assert(!std::is_polymorphic_v<Click>);

} // namespace deluge::dsp::fixed
