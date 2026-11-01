/*
 * Copyright © 2023-2026 Mark Adams, Synthstrom Audible Limited
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
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp-next/fixed/shaping/antialiased_tanh.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief Compressor: the RMS feedback compressor as one fused frame kernel, its knobs, and their mappings.
namespace deluge::dsp::fixed {

/// @brief The compressor's knob positions, each 0 to 2^31 (Q31 of the knob's travel).
struct CompressorKnobs {
	q31 attack{5 << 24};  ///< about 0.5 to 70 ms for a 3 dB change
	q31 release{5 << 24}; ///< about 50 to 400 ms for a 3 dB change
	q31 threshold{0};     ///< the threshold as a fraction of the volume's level in nepers: 0 is 1, 2^31 is 0.2
	q31 ratio{64 << 24};  ///< 0 is 2:1, 2^31 is 256:1
	q31 sidechain_hpf{0}; ///< the level detector's high-pass, about 0 to 100 Hz
	q31 blend{kOneQ31};   ///< the wet level, kOneQ31 fully wet
	bool operator==(const CompressorKnobs&) const = default;
};

/// @brief The attack time in ms for a 3 dB change at knob @p attack.
/// @param attack The knob position, Q31 of its travel.
/// @return The attack time, in ms.
[[nodiscard]] float compressor_attack_ms(q31 attack);
/// @brief The release time in ms for a 3 dB change at knob @p release.
/// @param release The knob position, Q31 of its travel.
/// @return The release time, in ms.
[[nodiscard]] float compressor_release_ms(q31 release);
/// @brief The ratio at knob @p ratio.
/// @param ratio The knob position, Q31 of its travel.
/// @return The ratio (2 to 256).
[[nodiscard]] float compressor_ratio(q31 ratio);
/// @brief The level detector's high-pass cutoff in Hz at knob @p sidechain_hpf.
/// @param sidechain_hpf The knob position, Q31 of its travel.
/// @return The cutoff, in Hz.
[[nodiscard]] float compressor_sidechain_hz(q31 sidechain_hpf);
/// @brief 1 - 1/ratio at knob @p ratio: how far a level change past the threshold is cut.
/// @param ratio The knob position, Q31 of its travel.
/// @return The fraction, 0.5 (2:1) to 1 (256:1).
[[nodiscard]] float ratio_fraction(q31 ratio);

/// @brief The RMS feedback compressor as a processor kernel of stereo frames: the gain for each block follows the RMS
/// level, in log space, of the block before it. Rendered in place as `Mono<Compressor>::render_block(block, block)`.
/// @details Levels and gains in the envelopes are in nepers (natural-log units).
///
/// Not composable into a Chain or Lanes, or rendered through `Mono::render(x)`, unless the owner calls @ref
/// finish_level after each block: the last frame's measurement and the level the next block follows are made there.
///
/// **Per block** (@ref prepare, from the levels the owner last set with @ref set_levels): the makeup envelope moves
/// toward the level the final volume asks for, the reduction envelope toward how far the last block's level passed the
/// threshold, and the channel gains get the increments that reach the gain they give by the block's end.
///
/// **Per frame** (@ref step, on @ref Loop alone):
/// 1. Each channel's gain ramps by its increment.
/// 2. Each channel is multiplied by its gain and saturated through an antialiased tanh at drive 3 (@ref kSaturator),
///    which keeps it within ±2^26.
/// 3. In @ref Mode::BLEND only, the frame as it came in is mixed back at the dry level: `dry_wet_blend`'s arithmetic
///    (fixed/mix/dry_wet.h). The step reads its input frame before the wrapper stores the output, so the blend runs in
///    place with no scratch.
/// 4. The frame before, held in `pending`, is measured: high-passed by each channel's detector, the larger unsigned
///    magnitude squared (the high word of an unsigned 32×32 product) into the block's 64-bit sum. A block of 128 frames
///    sums at most 2^37, so the sum never wraps, and a detector output of INT32_MIN measures 2^31. The measurement runs
///    one frame behind so that it is independent of this frame's gain and saturation and the core overlaps the two;
///    measured on the output it just made, the frame is one long dependency chain. @ref prepare sets `pending` to the
///    detectors' own memories, which high-pass to 0 and leave the memories as they are.
///
/// **After the block** (@ref finish_level), the block's last frame is measured, and the block's mean square, smoothed
/// against the last block's, gives the level the next block's gain follows.
///
/// **The owner's order per block:** @ref set_levels, @ref seed when not `seeded`, the block, then @ref finish_level,
/// which the block's level needs: its last frame is measured there. A reset (the wrapper's, through @ref reset) zeroes
/// the envelopes and the mean and unseeds the saturation; the channel gains, the detectors and the last level carry on.
struct Compressor {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.

	/// @brief Whether the output is blended with the input.
	enum class Mode : uint8_t {
		WET,   ///< Fully wet: the saturated frame.
		BLEND, ///< The saturated frame at the wet level plus the frame as it came in at the dry level.
	};
	/// @brief Both modes: one loop each, with no branch on the blend inside it.
	using Modes = ModeList<Mode::WET, Mode::BLEND>;

	/// @brief How hard the output is driven into the tanh: by 2^3.
	static constexpr uint32_t kSaturation = 3;
	/// @brief The output saturation's coefficients: the antialiased tanh at drive @ref kSaturation.
	static constexpr AntialiasedTanh::Coefficients kSaturator = AntialiasedTanh::coefficients({.drive = kSaturation});

	/// @brief What the owner sets: the knobs, and the makeup at no reduction.
	struct Settings {
		CompressorKnobs knobs{}; ///< The knobs; the threshold knob reaches the kernel per block, in @ref Levels.
		float base_gain = 1.35f; ///< Makeup gain in nepers at no reduction: 1.35, or 0.85 for the song's master.
	};

	/// @brief The knobs' rates, ratio, detector and blend, as the kernel and its owner read them.
	struct Coefficients {
		float attack_rate;        ///< Per sample, negative.
		float release_rate;       ///< Per sample, negative.
		float attack_ms;          ///< For a 3 dB change.
		float release_ms;         ///< For a 3 dB change.
		float fraction;           ///< 1 - 1/ratio, 0.5 to 1.
		float base_gain;          ///< Nepers.
		q31 detector_moveability; ///< The detector's high-pass moveability, Q31.
		q31 dry;                  ///< The dry level in the blend, Q31.
		q31 wet;                  ///< The wet level in the blend, Q31.
		Mode mode;                ///< WET when wet == kOneQ31.
	};

	/// @brief The block's inputs, which the owner sets before each block with @ref set_levels.
	struct Levels {
		q31 volume_l = 0;     ///< The left channel's linear gain, 4.27 signed.
		q31 volume_r = 0;     ///< The right channel's linear gain, 4.27 signed.
		q31 final_volume = 0; ///< The peak-to-peak volume scale, 3.29.
		q31 threshold = 0;    ///< Q31 of the knob's travel.
	};

	/// @brief What the loop steps: the channel gains, the saturation, the detectors, the frame waiting to be measured
	/// and the block's running sum.
	struct Loop {
		q31 current_volume_l = 0;              ///< The left channel's gain this frame, 5.26 signed.
		q31 current_volume_r = 0;              ///< The right channel's gain this frame, 5.26 signed.
		AntialiasedTanh::State saturation_l{}; ///< The left saturation's position on the curve.
		AntialiasedTanh::State saturation_r{}; ///< The right saturation's position on the curve.
		OnePole::State detector_l{};           ///< The left detector's one-pole memory, Q31.
		OnePole::State detector_r{};           ///< The right detector's one-pole memory, Q31.
		StereoSample pending{}; ///< The frame the next step measures, Q31: the last frame stepped, or after @ref
		                        ///< prepare the detectors' memories.
		int64_t sum = 0;        ///< The block's squared detector magnitudes, Q31, never wrapping.
	};

	/// @brief Everything that carries from block to block, the block's inputs, and its running measurement. Only
	/// @ref Loop is stepped per frame; the rest is @ref prepare's and the events'.
	struct State {
		Levels levels{};              ///< The levels the last @ref set_levels stored.
		float threshold = 1;          ///< Fraction of the final volume's level in nepers, 0.2 to 1.
		float makeup = 0;             ///< The makeup envelope, nepers: -23.07 to 2.08.
		float reduction_envelope = 0; ///< The reduction envelope, nepers past the threshold.
		float log_rms = 0;            ///< The log (nepers) of the last block's RMS level.
		float mean = 0;               ///< The last block's mean square level, 1 at full scale.
		Loop loop{};                  ///< What the loop steps.
		uint32_t n = 0;               ///< The block's length in frames, for @ref finish_level.
		bool seeded = false;          ///< Whether the saturation is seeded since the last reset.
		uint8_t gain_reduction = 0;   ///< The block's gain reduction for the LEDs, 0 to 127.
	};

	/// @brief The wrappers copy only @ref Loop around the block's loop: the levels, envelopes and mean are touched once
	/// per block.
	static constexpr auto kLoopState = &State::loop;

	/// @brief The block's gain ramps, from @ref prepare.
	struct Block {
		q31 increment_l; ///< The left gain's step per frame, 5.26 signed.
		q31 increment_r; ///< The right gain's step per frame, 5.26 signed.
	};

	/// @brief The coefficients for @p s: the knobs' time constants, ratio, detector high-pass and blend.
	/// @param s What the owner sets.
	/// @return The coefficients the kernel and its owner read.
	[[nodiscard]] static Coefficients coefficients(const Settings& s);

	/// @brief The block's envelopes and gain ramps, from the levels last set and the last block's level.
	/// @details The makeup envelope moves toward the level `final_volume` asks for, the reduction envelope toward how
	/// far the last level passed the threshold, and the gain they give (clamped to 31) sets each channel's target from
	/// its volume; the increments reach the targets by the block's end. Sets `gain_reduction`, zeroes `sum`, records
	/// @p n and sets `pending` to the detectors' memories.
	/// @param c The coefficients.
	/// @param s The State, whose envelopes and loop are updated for the block.
	/// @param n The block's length in frames, at least 1: the compressor's owner skips empty blocks.
	/// @return The gain ramps for the block.
	[[nodiscard]] [[gnu::always_inline]] static Block prepare(const Coefficients& c, State& s, std::size_t n) {
		const auto frames = static_cast<float>(n);

		// The offset keeps a final volume of zero from giving NaNs. At most 2.08 nepers, at 0x7fffffff (about 8 in
		// 3.29).
		const float song_volume = logf(s.levels.final_volume + 1e-10);
		const float threshold = song_volume * s.threshold;
		// The song volume lands here, so the envelope smooths it against clicks; the makeup gain and the reduction
		// then move at the same rate.
		// The clamps compare as std::max and std::min do, which are out-of-line calls where the app instantiates this.
		const float makeup = (song_volume - threshold - 1) * c.fraction;
		s.makeup = run_envelope(c, s.makeup, makeup < 0 ? 0 : makeup, frames);

		const float over = s.log_rms - threshold;
		s.reduction_envelope = run_envelope(c, s.reduction_envelope, 0 < over ? over : 0, frames);

		const float reduction = -s.reduction_envelope * c.fraction;

		// Nepers of gain: above 3.43 the gain would pass 31, the most there is headroom for.
		const float gain_nepers = c.base_gain + s.makeup + reduction;
		const float exp_gain = std::exp(gain_nepers);
		const float gain = 31 < exp_gain ? 31 : exp_gain;

		// The channels' target gains, 13.18 signed, held as floats on the way to the increments.
		const float final_volume_l = gain * static_cast<float>(s.levels.volume_l >> 9);
		const float final_volume_r = gain * static_cast<float>(s.levels.volume_r >> 9);

		// × 4 for dB, then × 4 again for the LEDs' range, since a 30 dB reduction is all but silence.
		const auto leds = static_cast<int32_t>(-(reduction) * 4 * 4);
		s.gain_reduction = static_cast<uint8_t>(leds < 0 ? 0 : leds > 127 ? 127 : leds);
		s.loop.sum = 0;
		s.n = static_cast<uint32_t>(n);
		// Each detector's own memory as the frame before the first: it high-passes to 0 and leaves the memory as it is.
		s.loop.pending = {.l = s.loop.detector_l.z, .r = s.loop.detector_r.z};

		// The steps (5.26 signed) that reach the target gains by the end of the block.
		return {
		    .increment_l = static_cast<int32_t>((final_volume_l - (s.loop.current_volume_l >> 8)) / frames) << 8,
		    .increment_r = static_cast<int32_t>((final_volume_r - (s.loop.current_volume_r >> 8)) / frames) << 8,
		};
	}

	/// @brief One frame at mode @p M: ramp, gain, saturate, blend in BLEND, then measure the frame before.
	/// @param c The coefficients.
	/// @param b The block's gain ramps from @ref prepare.
	/// @param s The loop state.
	/// @param x The frame as it came in, Q31: the dry signal in BLEND.
	/// @return The compressed frame, Q31.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static StereoSample step(const Coefficients& c, const Block& b, Loop& s,
	                                                              StereoSample x) {
		s.current_volume_l += b.increment_l;
		s.current_volume_r += b.increment_r;

		// << 4: the gains are 5.26 signed rather than 1.30.
		StereoSample y{
		    .l = AntialiasedTanh::step(kSaturator, EmptyBlock{}, s.saturation_l,
		                               multiply_32x32_rshift32(x.l, s.current_volume_l) << 4),
		    .r = AntialiasedTanh::step(kSaturator, EmptyBlock{}, s.saturation_r,
		                               multiply_32x32_rshift32(x.r, s.current_volume_r) << 4),
		};

		if constexpr (M == Mode::BLEND) {
			// dry_wet_blend's arithmetic (fixed/mix/dry_wet.h). << 1 makes up for the two multiplies.
			y.l = multiply_accumulate_32x32_rshift32_rounded(multiply_32x32_rshift32(y.l, c.wet), x.l, c.dry) << 1;
			y.r = multiply_accumulate_32x32_rshift32_rounded(multiply_32x32_rshift32(y.r, c.wet), x.r, c.dry) << 1;
		}

		measure(c, s, s.pending);
		s.pending = y;
		return y;
	}

	/// @brief The reset quirk: no gain reduction or makeup at the start of the next block, the mean at 0, and the
	/// saturation seeded again. The channel gains, the detectors and the last level carry on.
	/// @param s The State to reset.
	static void reset(State& s);

	/// @brief The threshold as a fraction of the final volume's level in nepers, from knob @p threshold.
	/// @param threshold The knob position, Q31 of its travel.
	/// @return The fraction: 1 at 0, 0.2 at 2^31.
	[[nodiscard]] static float threshold_fraction(q31 threshold) {
		return 1 - 0.8f * (static_cast<float>(threshold) / kOneQ31f);
	}

	/// @brief Event: the block's levels, and the threshold @p l's knob gives as a fraction of the volume's level.
	/// @details Inline: the owner calls it once per block, and its few instructions cost less than a call.
	/// @param s The State to store the levels in.
	/// @param l The block's levels.
	[[gnu::always_inline]] static void set_levels(State& s, const Levels& l) {
		s.levels = l;
		s.threshold = threshold_fraction(l.threshold);
	}

	/// @brief Event: seeds both saturations from @p first, the block's first frame as it comes in, so it interpolates
	/// against itself rather than against silence; marks the state seeded.
	/// @param s The State to seed.
	/// @param first The block's first frame, Q31.
	static void seed(State& s, StereoSample first);

	/// @brief Event, after the block: measures the block's last frame, then the block's mean square (never negative),
	/// smoothed one-pole against the last block's, and its log RMS level (nepers), which the next block's gain follows.
	/// @param c The coefficients.
	/// @param s The State holding the block's running sum.
	static void finish_level(const Coefficients& c, State& s);

private:
	/// @brief Adds output frame @p y to the block's sum: high-passed by each channel's detector, the larger unsigned
	/// magnitude squared (the high word of an unsigned 32×32 product).
	[[gnu::always_inline]] static void measure(const Coefficients& c, Loop& s, StereoSample y) {
		// The output less its one-pole low-pass, wrapping as the target's `sub` does.
		const OnePole::Coefficients detector{.m = c.detector_moveability, .mode = OnePole::Mode::HIGH_PASS};
		const uint32_t l =
		    magnitude(OnePole::step<OnePole::Mode::HIGH_PASS>(detector, EmptyBlock{}, s.detector_l, y.l));
		const uint32_t r =
		    magnitude(OnePole::step<OnePole::Mode::HIGH_PASS>(detector, EmptyBlock{}, s.detector_r, y.r));
		const uint64_t larger = l > r ? l : r;
		s.sum += static_cast<int64_t>((larger * larger) >> 32);
	}

	/// @return |@p x| as an unsigned value: INT32_MIN gives 2^31.
	[[nodiscard]] [[gnu::always_inline]] static uint32_t magnitude(q31 x) {
		return x < 0 ? 0u - static_cast<uint32_t>(x) : static_cast<uint32_t>(x);
	}

	/// @return @p current moved toward @p desired over @p frames: at the attack rate when rising, the release rate
	/// otherwise.
	[[nodiscard]] [[gnu::always_inline]] static float run_envelope(const Coefficients& c, float current, float desired,
	                                                               float frames) {
		const float rate = desired > current ? c.attack_rate : c.release_rate;
		return desired + std::exp(rate * frames) * (current - desired);
	}
};

static_assert(processor_kernel<Compressor>);
static_assert(moded_kernel<Compressor>);
static_assert(!std::is_polymorphic_v<Compressor>);

} // namespace deluge::dsp::fixed
