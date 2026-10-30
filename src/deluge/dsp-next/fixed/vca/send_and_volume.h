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
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/vca/pan.h"
#include "util/fixedpoint.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief The reverb send, and the post-FX and post-reverb volumes with pan ramped across a block, as a processor
/// kernel over stereo frames.
namespace deluge::dsp::fixed {

/// @brief The shift from a gain product to the volume scale, where 134217728 (2^27) is unity.
inline constexpr int kVolumeShift = 5;
/// @brief The shift back from a gain scaled by a pan amplitude.
inline constexpr int kPanShift = 2;
/// @brief The shift from the send's product to the reverb input.
inline constexpr int kReverbSendShift = 1;

/// @brief One frame's reverb send: (l + r) × @p send_gain, high word, doubled.
/// @details Taken before the frame's volume is applied. The left and right sum wraps as the target's `add` does.
/// @param x The frame, Q31.
/// @param send_gain The post-FX volume times the send, ≪ 5.
/// @return The frame's contribution to the reverb input.
[[nodiscard]] [[gnu::always_inline]] inline q31 reverb_send(StereoSample x, q31 send_gain) {
	return multiply_32x32_rshift32(wrapping_add(x.l, x.r), send_gain) << kReverbSendShift;
}

/// @brief The post-FX and post-reverb volumes, with pan, applied to stereo frames; the post-reverb volume ramps across
/// the block from the last block's.
/// @details Each frame is (x × gain) ≫ 32 ≪ 5 per channel, the gain (post-reverb × post-FX) ≫ 32 ≪ 5, each channel's
/// scaled by its pan amplitude ≫ 32 ≪ 2 when the engine renders stereo and the pan is off-centre.
///
/// **The ramp's quirk.** The gains start at this block's volume, not the last block's, and step before the first
/// frame, so a ramp ends one block's change past its target; the next block starts again at its own volume. The
/// per-frame step divides the volume change by the block length in double, truncating toward zero; the change wraps
/// as the target's `sub` does, from a volume at q31's minimum.
///
/// **Events.** @ref start_from sets the volume a first ramp starts from, -1 being unset, as the owners construct it;
/// @ref forget unsets it; @ref prime sets it only while unset. An unset volume ramps from -1, as it always has.
struct SendAndVolume {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.

	/// @brief Whether the post-reverb volume ramps across the block.
	enum class Mode : uint8_t {
		HOLD, ///< The gains hold for the block.
		RAMP, ///< The gains step once per frame, before the frame.
	};
	using Modes = ModeList<Mode::HOLD, Mode::RAMP>; ///< Both read the same gains.

	/// @brief The volume that marks the ramp's start as unset.
	static constexpr q31 kUnset = -1;

	/// @brief What the owner sets each block.
	struct Settings {
		q31 post_fx_volume = 0;     ///< Q31, 2^27 unity.
		q31 post_reverb_volume = 0; ///< Q31, 2^27 unity.
		q31 pan = 0;                ///< Q31, 0 centred.
		bool ramp = false;          ///< Whether to ramp from the last block's post-reverb volume.
		bool stereo = true;         ///< Whether the engine renders stereo; the pan applies only then.
	};

	/// @brief The block's volumes and pan, as set.
	struct Coefficients {
		q31 post_fx_volume = 0;     ///< Q31.
		q31 post_reverb_volume = 0; ///< Q31.
		PanAmplitudes pan{};        ///< The pan's amplitudes, read only when @ref panned.
		bool panned = false;        ///< Off-centre, with the engine rendering stereo.
		Mode mode = Mode::HOLD;     ///< RAMP when Settings::ramp.
	};

	/// @brief The running gains the loop steps.
	struct Gains {
		q31 l = 0; ///< Left gain.
		q31 r = 0; ///< Right gain.
	};

	/// @brief The gains and the volume the next ramp starts from.
	struct State {
		Gains gains{};                   ///< The loop state, set by each block's prepare.
		q31 last_post_reverb_volume = 0; ///< The last block's post-reverb volume, or kUnset.
		bool primed = true;              ///< False after @ref forget, until @ref prime.
	};

	/// @brief The per-frame steps of a ramping block.
	struct Block {
		q31 step_l = 0; ///< Added to the left gain before each frame.
		q31 step_r = 0; ///< Added to the right gain before each frame.
	};

	static constexpr auto kLoopState = &State::gains; ///< The loop steps the gains only.

	/// @brief The coefficients for @p s.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) {
		const bool panned = s.pan != 0 && s.stereo;
		return {
		    .post_fx_volume = s.post_fx_volume,
		    .post_reverb_volume = s.post_reverb_volume,
		    .pan = panned ? pan_amplitudes(s.pan) : PanAmplitudes{},
		    .panned = panned,
		    .mode = s.ramp ? Mode::RAMP : Mode::HOLD,
		};
	}

	/// @brief Starts the gains at this block's volumes, works out the ramp's step from the last block's post-reverb
	/// volume over @p n frames, and records this block's.
	/// @param c The block's volumes and pan.
	/// @param s The gains, set; the last volume, read and then replaced.
	/// @param n The block's length, at least 1.
	/// @return The per-frame steps (zero unless ramping).
	[[nodiscard]] [[gnu::always_inline]] static Block prepare(const Coefficients& c, State& s, std::size_t n) {
		q31 gain_l = multiply_32x32_rshift32(c.post_reverb_volume, c.post_fx_volume) << kVolumeShift;
		q31 gain_r = gain_l;
		Block b{};
		if (c.mode == Mode::RAMP) {
			const auto volume_step =
			    static_cast<q31>(static_cast<double>(wrapping_sub(c.post_reverb_volume, s.last_post_reverb_volume))
			                     / static_cast<double>(n));
			b.step_l = b.step_r = multiply_32x32_rshift32(c.post_fx_volume, volume_step) << kVolumeShift;
		}
		if (c.panned) {
			gain_l = multiply_32x32_rshift32(gain_l, c.pan.left) << kPanShift;
			gain_r = multiply_32x32_rshift32(gain_r, c.pan.right) << kPanShift;
			b.step_l = multiply_32x32_rshift32(b.step_l, c.pan.left) << kPanShift;
			b.step_r = multiply_32x32_rshift32(b.step_r, c.pan.right) << kPanShift;
		}
		s.gains = {.l = gain_l, .r = gain_r};
		s.last_post_reverb_volume = c.post_reverb_volume;
		s.primed = true;
		return b;
	}

	/// @brief One frame: step the gains if ramping, then apply them.
	/// @tparam M The block's mode.
	/// @param b The block's steps.
	/// @param g The running gains.
	/// @param x The frame, Q31.
	/// @return The frame at the gains.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static StereoSample step(const Coefficients& /*c*/, const Block& b, Gains& g,
	                                                              StereoSample x) {
		if constexpr (M == Mode::RAMP) {
			g.l = wrapping_add(g.l, b.step_l);
			g.r = wrapping_add(g.r, b.step_r);
		}
		return {.l = multiply_32x32_rshift32(x.l, g.l) << kVolumeShift,
		        .r = multiply_32x32_rshift32(x.r, g.r) << kVolumeShift};
	}

	/// @brief Sets the volume a first ramp starts from; @ref kUnset leaves it for @ref prime.
	/// @param s The state.
	/// @param post_reverb_volume The volume, Q31, or kUnset.
	static void start_from(State& s, q31 post_reverb_volume) {
		s.last_post_reverb_volume = post_reverb_volume;
		s.primed = post_reverb_volume != kUnset;
	}

	/// @brief Unsets the volume the next ramp starts from.
	/// @param s The state.
	static void forget(State& s) {
		s.last_post_reverb_volume = kUnset;
		s.primed = false;
	}

	/// @brief Sets the volume the next ramp starts from, if unset.
	/// @param s The state.
	/// @param post_reverb_volume The volume, Q31.
	static void prime(State& s, q31 post_reverb_volume) {
		if (!s.primed) {
			s.last_post_reverb_volume = post_reverb_volume;
			s.primed = true;
		}
	}
};

static_assert(processor_kernel<SendAndVolume>);
static_assert(!std::is_polymorphic_v<SendAndVolume>);

} // namespace deluge::dsp::fixed
