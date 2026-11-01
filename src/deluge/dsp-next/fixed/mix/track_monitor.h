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
#include "dsp-next/core/mode.h"
#include "dsp-next/core/q31_ops.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief TrackMonitor: an audio track's monitoring, the input at a ramped amplitude folded into its output, as a mixer
/// kernel. The output stage's fold is @ref output_monitor_fold (output_fold.h).
namespace deluge::dsp::fixed {

/// @brief An audio track's monitoring: the input frame at an amplitude ramped across the block, folded, added to the
/// track's output frame.
/// @details The amplitude starts at the block's start value and steps before each frame by (end − start) ÷ n, in
/// double, truncated toward zero, so the last frame is at the end value. Each channel is (input × amplitude) ≫ 32 ≪ 2.
/// BALANCED and COMBINE halve each channel by division (toward zero), not by a shift. Sums and differences wrap as the
/// target's `add` and `sub` do.
struct TrackMonitor {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.

	/// @brief How the input frame folds.
	enum class Mode : uint8_t {
		STEREO,   ///< Both channels.
		LEFT,     ///< The left to both.
		RIGHT,    ///< The right to both.
		BALANCED, ///< l/2 − r/2 to both.
		COMBINE,  ///< l/2 + r/2 to both: a stereo input on a mono render.
	};
	using Modes = ModeList<Mode::STEREO, Mode::LEFT, Mode::RIGHT, Mode::BALANCED, Mode::COMBINE>; ///< One ramp.

	static constexpr int kAmplitudeShift = 2; ///< The shift after each channel's amplitude product.

	/// @brief The block's ramp and fold.
	struct Settings {
		q31 amplitude_start = 0;  ///< The track's amplitude at the block's start.
		q31 amplitude_end = 0;    ///< At its end.
		Mode mode = Mode::STEREO; ///< The fold.
	};
	/// @brief The block's ramp and fold, as set.
	struct Coefficients {
		q31 amplitude_start = 0;  ///< The ramp's start.
		q31 amplitude_end = 0;    ///< The ramp's end.
		Mode mode = Mode::STEREO; ///< The fold.
	};
	/// @brief The running amplitude, set by each block's prepare.
	struct State {
		q31 amplitude = 0; ///< The amplitude the last frame took.
	};
	/// @brief The block's per-frame step.
	struct Block {
		q31 increment = 0; ///< Added to the amplitude before each frame.
	};

	/// @brief The ramp and fold, unchanged.
	[[nodiscard]] static constexpr Coefficients coefficients(const Settings& s) {
		return {.amplitude_start = s.amplitude_start, .amplitude_end = s.amplitude_end, .mode = s.mode};
	}

	/// @brief Starts the amplitude at the block's start and works out its step over @p n frames.
	[[nodiscard]] [[gnu::always_inline]] static Block prepare(const Coefficients& c, State& s, std::size_t n) {
		s.amplitude = c.amplitude_start;
		return {.increment = static_cast<q31>(static_cast<double>(wrapping_sub(c.amplitude_end, c.amplitude_start))
		                                      / static_cast<double>(n))};
	}

	/// @brief One frame: @p output plus @p input at the stepped amplitude, folded by @p M.
	/// @tparam M The fold.
	/// @param blk The block's step.
	/// @param s The running amplitude.
	/// @param output The track's frame, Q31.
	/// @param input The input frame, Q31.
	/// @return @p output with the folded input added.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static StereoSample step(const Coefficients& /*c*/, const Block& blk, State& s,
	                                                              StereoSample output, StereoSample input) {
		s.amplitude = wrapping_add(s.amplitude, blk.increment);
		const StereoSample in{.l = multiply_32x32_rshift32(input.l, s.amplitude) << kAmplitudeShift,
		                      .r = multiply_32x32_rshift32(input.r, s.amplitude) << kAmplitudeShift};
		StereoSample add = in;
		if constexpr (M == Mode::LEFT) {
			add = StereoSample::from_mono(in.l);
		}
		else if constexpr (M == Mode::RIGHT) {
			add = StereoSample::from_mono(in.r);
		}
		else if constexpr (M == Mode::BALANCED) {
			add = StereoSample::from_mono(wrapping_sub(in.l / 2, in.r / 2));
		}
		else if constexpr (M == Mode::COMBINE) {
			add = StereoSample::from_mono(wrapping_add(in.l / 2, in.r / 2));
		}
		return {.l = wrapping_add(output.l, add.l), .r = wrapping_add(output.r, add.r)};
	}
};

static_assert(mixer_kernel<TrackMonitor>);
static_assert(!std::is_polymorphic_v<TrackMonitor>);

} // namespace deluge::dsp::fixed
