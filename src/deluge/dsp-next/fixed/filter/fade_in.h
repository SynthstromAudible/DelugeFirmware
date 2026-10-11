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
#include "dsp-next/fixed/mix/dry_wet.h"
#include "util/fixedpoint.h"

#include <type_traits>

/// @file
/// @brief FadeIn: the fade a filter runs after being switched on, from its dry input to its output, as a mixer kernel
/// over stereo frames (FadeIn) and over mono samples (FadeInMono).
namespace deluge::dsp::fixed {

/// @brief The filters' fade-in over stereo frames: a mixer of the dry input and the filtered (wet) signal, the wet
/// level rising over about 690 samples.
/// @details Each sample is @ref dry_wet_blend of dry and wet at the current wet level, and then the level steps: the
/// dry level, a float, × 0.99 (in double, stored as float), and the wet level ONE_Q31 × (1 − dry) in float, to Q31.
/// The fade is over once dry falls below 0.001 (compared as a double, @ref active); the owner runs the kernel only
/// on blocks where it is active, so the level stops stepping there.
///
/// **Two steps per frame.** The level steps once per channel-sample: left blends at one level, steps, right blends
/// at the next, steps again. So a stereo fade ends in half the frames of a mono one, and its channels sit one step
/// apart. That is why this is a StereoSample kernel under `Mono`, not a q31 kernel under `Stereo`: `Stereo` (Lanes)
/// gives each channel its own State, so the two channels would each step their own level once per frame. Here the
/// one level is stepped twice per frame, left then right. @ref FadeInMono is the mono form, one step per sample,
/// sharing this State and these events.
///
/// **Events.** @ref begin starts a fade (restarting one under way); @ref zero is the filter set's zeroing, which
/// leaves no fade. Run them through the wrapper's `on`.
struct FadeIn {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.

	/// @brief Nothing to set: the fade's course is fixed.
	struct Settings {};
	/// @brief Nothing to read but the State.
	struct Coefficients {};

	/// @brief The levels. State{} is a fresh filter's: dry 1, wet ONE_Q31, which is active.
	struct State {
		float dry = 1.0F;  ///< The dry level, 1 at the start of a fade, × 0.99 per step.
		q31 wet = ONE_Q31; ///< The wet level the next sample blends at, Q31.
	};

	/// @brief The dry level below which the fade is over, compared as a double.
	static constexpr double kEnd = 0.001;
	/// @brief Each step's factor on the dry level, applied in double.
	static constexpr double kDecay = 0.99;

	/// @brief The coefficients: none.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& /*s*/) {
		return {};
	}

	/// @brief Whether a fade is under way: dry not below 0.001, the float compared as a double (a NaN counts as under
	/// way).
	[[nodiscard]] [[gnu::always_inline]] static bool active(const State& s) { return !(s.dry < kEnd); }

	/// @brief Starts fading in from dry: dry 1, wet 0. A fade under way restarts.
	[[gnu::always_inline]] static void begin(State& s) { s = {.dry = 1.0F, .wet = 0}; }

	/// @brief The filter set's zeroing: dry 0 and wet 0, no fade.
	[[gnu::always_inline]] static void zero(State& s) { s = {.dry = 0.0F, .wet = 0}; }

	/// @brief One channel-sample: the blend at the current level, then the level's step.
	/// @param s The levels, stepped once.
	/// @param dry The dry sample, Q31.
	/// @param wet The filtered sample, Q31.
	/// @return dry_wet_blend(dry, wet, level) at the level before the step, Q31.
	[[nodiscard]] [[gnu::always_inline]] static q31 blend_step(State& s, q31 dry, q31 wet) {
		const q31 out = dry_wet_blend(dry, wet, s.wet);
		s.dry = static_cast<float>(s.dry * kDecay);
		s.wet = static_cast<q31>(static_cast<float>(ONE_Q31) * (1.0F - s.dry));
		return out;
	}

	/// @brief One frame: the left channel blended and the level stepped, then the right.
	/// @param s The levels, stepped twice.
	/// @param dry The dry frame, Q31.
	/// @param wet The filtered frame, Q31.
	/// @return The blended frame, Q31.
	[[nodiscard]] [[gnu::always_inline]] static StereoSample step(const Coefficients& /*c*/, const EmptyBlock& /*b*/,
	                                                              State& s, StereoSample dry, StereoSample wet) {
		const q31 l = blend_step(s, dry.l, wet.l);
		const q31 r = blend_step(s, dry.r, wet.r);
		return {.l = l, .r = r};
	}
};

/// @brief The filters' fade-in over mono samples: @ref FadeIn's blend and level, one step per sample, on the same
/// State and through the same events (`on<&FadeIn::begin>`, `on<&FadeIn::zero>`).
struct FadeInMono {
	using value_type = q31; ///< A sample, Q31.
	using step_type = q31;  ///< One sample per step.

	using Settings = FadeIn::Settings;         ///< Nothing to set.
	using Coefficients = FadeIn::Coefficients; ///< Nothing to read but the State.
	using State = FadeIn::State;               ///< The levels, shared with the stereo form.

	/// @brief The coefficients: none.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& /*s*/) {
		return {};
	}

	/// @brief One sample: the blend at the current level, then the level's step.
	/// @param s The levels, stepped once.
	/// @param dry The dry sample, Q31.
	/// @param wet The filtered sample, Q31.
	/// @return The blend, Q31.
	[[nodiscard]] [[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                                     q31 dry, q31 wet) {
		return FadeIn::blend_step(s, dry, wet);
	}
};

static_assert(mixer_kernel<FadeIn>);
static_assert(mixer_kernel<FadeInMono>);
static_assert(!std::is_polymorphic_v<FadeIn>);
static_assert(!std::is_polymorphic_v<FadeInMono>);

} // namespace deluge::dsp::fixed
