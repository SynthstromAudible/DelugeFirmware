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

#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/vca/pan.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief SendAndVolume: the reverb send, and the post-FX and post-reverb volumes with pan ramped across a block, as a
/// processor kernel over stereo frames.
namespace deluge::dsp::fixed {

/// @brief The reverb send, then the post-FX and post-reverb volumes with pan, over stereo frames; the post-reverb
/// volume ramps across the block from the last block's.
/// @details The volumes and gains are Q27 (2^27 unity), the pan amplitudes Q30, the send and the frames Q31. Every
/// product is the high word, reformatted back to its operands' meaning with a wrapping shift.
///
/// Per frame: the send, (l + r) × the send gain, is added to the reverb input at the send cursor, read before the
/// frame's volume; then each channel is x × its gain. The gain is post-reverb × post-FX, each channel's scaled by its
/// pan amplitude when the engine renders stereo and the pan is off-centre; the send gain is post-FX × send, Q31. Sums
/// wrap as the target's `add` does.
///
/// **The send cursor is a second output.** The reverb input is a value per frame added to a buffer the owner
/// supplies: @ref set_send points the cursor at it each block, and the frame step adds to it and advances it, as
/// `Granular`'s does. The SEND modes run it; with the send at 0 the cursor is not read.
///
/// **The ramp's quirk.** The gains start at this block's volume, not the last block's, and step before the first
/// frame, so a ramp ends one block's change past its target; the next block starts again at its own volume. The
/// per-frame step divides the volume change by the block length in double, truncating toward zero; the change wraps
/// as the target's `sub` does.
///
/// **Events.** @ref start_from sets the volume a first ramp starts from, @ref kUnset (-1) leaving it for @ref prime;
/// @ref forget unsets it; @ref prime sets it only while it is @ref kUnset. An unset volume ramps from -1.
struct SendAndVolume {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.

	/// @brief Whether the post-reverb volume ramps across the block, and whether the frames feed the reverb.
	enum class Mode : uint8_t {
		HOLD,      ///< The gains hold for the block; no send.
		RAMP,      ///< The gains step once per frame, before the frame; no send.
		HOLD_SEND, ///< HOLD, each frame first sent to the reverb.
		RAMP_SEND, ///< RAMP, each frame first sent to the reverb.
	};
	using Modes = ModeList<Mode::HOLD, Mode::RAMP, Mode::HOLD_SEND, Mode::RAMP_SEND>; ///< One gain law.

	/// @brief The volume scale: Q27, 134217728 (2^27) is unity.
	using Volume = Q<27>;
	/// @brief The volume that marks the ramp's start as unset.
	static constexpr Volume kUnset = Volume::from_raw(-1);

	/// @brief What the owner sets each block.
	struct Settings {
		Volume post_fx_volume{};     ///< 2^27 unity.
		Volume post_reverb_volume{}; ///< 2^27 unity.
		Q<31> reverb_send{};         ///< 0 sends nothing.
		Q<31> pan{};                 ///< 0 centred.
		bool ramp = false;           ///< Whether to ramp from the last block's post-reverb volume.
		bool stereo = true;          ///< Whether the engine renders stereo; the pan applies only then.
	};

	/// @brief The block's volumes, send gain and pan.
	struct Coefficients {
		Volume post_fx_volume{};     ///< The post-FX volume.
		Volume post_reverb_volume{}; ///< The post-reverb volume.
		Q<31> send_gain{};           ///< post-FX × send.
		PanAmplitudes pan{};         ///< The pan's amplitudes, read only when @ref panned.
		bool panned = false;         ///< Off-centre, with the engine rendering stereo.
		Mode mode = Mode::HOLD;      ///< RAMP when Settings::ramp; a SEND mode when the send is non-zero.
	};

	/// @brief What the loop steps: the running gains and the send cursor.
	struct Loop {
		Volume gain_l{};       ///< Left gain.
		Volume gain_r{};       ///< Right gain.
		Q<31>* send = nullptr; ///< The reverb input at the next frame, set by @ref set_send.
	};

	/// @brief The loop's state and the volume the next ramp starts from.
	struct State {
		Loop loop{};                      ///< The gains, set by each block's prepare, and the send cursor.
		Volume last_post_reverb_volume{}; ///< The last block's post-reverb volume, or kUnset.
	};

	/// @brief The per-frame steps of a ramping block.
	struct Block {
		Volume step_l{}; ///< Added to the left gain before each frame.
		Volume step_r{}; ///< Added to the right gain before each frame.
	};

	static constexpr auto kLoopState = &State::loop; ///< The loop steps the gains and the cursor.

	/// @brief One frame's reverb send: (l + r) × @p send_gain, Q31.
	[[nodiscard]] [[gnu::always_inline]] static Q<31> send_of(StereoSample x, Q<31> send_gain) {
		return mul_as_wrap<Q<31>>(add_wrap(x.l, x.r), send_gain);
	}

	/// @brief The coefficients for @p s.
	[[nodiscard]] [[gnu::always_inline]] static Coefficients coefficients(const Settings& s) {
		const bool panned = s.pan != Q<31>{} && s.stereo;
		const bool send = s.reverb_send != Q<31>{};
		return {
		    .post_fx_volume = s.post_fx_volume,
		    .post_reverb_volume = s.post_reverb_volume,
		    .send_gain = mul_as_wrap<Q<31>>(s.post_fx_volume, s.reverb_send),
		    .pan = panned ? pan_amplitudes(s.pan) : PanAmplitudes{},
		    .panned = panned,
		    .mode = s.ramp ? (send ? Mode::RAMP_SEND : Mode::RAMP) : (send ? Mode::HOLD_SEND : Mode::HOLD),
		};
	}

	/// @brief Starts the gains at this block's volumes, works out the ramp's step from the last block's post-reverb
	/// volume over @p n frames, and records this block's.
	/// @param c The block's volumes and pan.
	/// @param s The gains, set; the last volume, read and then replaced.
	/// @param n The block's length, at least 1.
	/// @return The per-frame steps (zero unless ramping).
	[[nodiscard]] [[gnu::always_inline]] static Block prepare(const Coefficients& c, State& s, std::size_t n) {
		const Volume post_fx = c.post_fx_volume;
		auto gain_l = mul_as_wrap<Volume>(c.post_reverb_volume, post_fx);
		Volume gain_r = gain_l;
		Volume step_l{};
		Volume step_r{};
		if (c.mode == Mode::RAMP || c.mode == Mode::RAMP_SEND) {
			// The change over the block's length in double, truncated: a float cast has no typed form
			const auto volume_step = Volume::from_raw(static_cast<int32_t>(
			    static_cast<double>(sub_wrap(c.post_reverb_volume, s.last_post_reverb_volume).raw())
			    / static_cast<double>(n)));
			step_l = step_r = mul_as_wrap<Volume>(post_fx, volume_step);
		}
		if (c.panned) {
			const Q<30> pan_l = c.pan.left;
			const Q<30> pan_r = c.pan.right;
			gain_l = mul_as_wrap<Volume>(gain_l, pan_l);
			gain_r = mul_as_wrap<Volume>(gain_r, pan_r);
			step_l = mul_as_wrap<Volume>(step_l, pan_l);
			step_r = mul_as_wrap<Volume>(step_r, pan_r);
		}
		const Block b{.step_l = step_l, .step_r = step_r};
		s.loop.gain_l = gain_l;
		s.loop.gain_r = gain_r;
		s.last_post_reverb_volume = c.post_reverb_volume;
		return b;
	}

	/// @brief One frame: its send, if sending; the gains' step, if ramping; then the gains.
	/// @tparam M The block's mode.
	/// @param c The send gain.
	/// @param b The block's steps.
	/// @param l The running gains and the send cursor.
	/// @param x The frame, Q31.
	/// @return The frame at the gains.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static StereoSample step(const Coefficients& c, const Block& b, Loop& l,
	                                                              StereoSample x) {
		if constexpr (M == Mode::HOLD_SEND || M == Mode::RAMP_SEND) {
			*l.send = add_wrap(*l.send, send_of(x, c.send_gain));
			++l.send;
		}
		if constexpr (M == Mode::RAMP || M == Mode::RAMP_SEND) {
			l.gain_l = add_wrap(l.gain_l, b.step_l);
			l.gain_r = add_wrap(l.gain_r, b.step_r);
		}
		return {.l = mul_as_wrap<Q<31>>(x.l, l.gain_l), .r = mul_as_wrap<Q<31>>(x.r, l.gain_r)};
	}

	/// @brief Points the send cursor at the reverb input for the next block's first frame.
	/// @param s The state.
	/// @param send The reverb input, one value per frame of the block.
	static void set_send(State& s, Q<31>* send) { s.loop.send = send; }

	/// @brief Sets the volume a first ramp starts from; @ref kUnset leaves it for @ref prime.
	/// @param s The state.
	/// @param post_reverb_volume The volume, Q27, or kUnset.
	static void start_from(State& s, Volume post_reverb_volume) { s.last_post_reverb_volume = post_reverb_volume; }

	/// @brief Unsets the volume the next ramp starts from.
	/// @param s The state.
	static void forget(State& s) { s.last_post_reverb_volume = kUnset; }

	/// @brief Sets the volume the next ramp starts from, while it is @ref kUnset.
	/// @param s The state.
	/// @param post_reverb_volume The volume, Q27.
	static void prime(State& s, Volume post_reverb_volume) {
		if (s.last_post_reverb_volume == kUnset) {
			s.last_post_reverb_volume = post_reverb_volume;
		}
	}
};

static_assert(processor_kernel<SendAndVolume>);
static_assert(!std::is_polymorphic_v<SendAndVolume>);

} // namespace deluge::dsp::fixed
