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
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/filter/filter_modes.h"
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp-next/fixed/shaping/antialiased_tanh.h"
#include "dsp-next/fixed/shaping/tanh.h"

#include <algorithm>
#include <cstdint>
#include <tuple>
#include <type_traits>

/// @file
/// @brief HighPassLadder: the transistor-ladder high-pass as a processor kernel.
namespace deluge::dsp::fixed {

/// @brief The transistor-ladder high-pass: three one-pole sections (@ref OnePole) around a resonant loop, saturated at
/// high resonance.
/// @details Per sample, with the sections' memories (Q31) and the block's moveability m:
///
///     m' = max(m + x·2^4 × morph, −2^23),   h = HP₁(x),   a = (h + fb₃ z₃ + fb₂ z₂) × D
///
/// where HP₁ steps the first section at m' as a high-pass (@ref OnePole::Mode::HIGH_PASS: x less its low-pass), z₃
/// and z₂ are the third and second sections' memories, fb₃ and fb₂ their feedbacks, and D the reciprocal of the loop
/// gain. a is saturated by the block's mode, then fed through the third section's high-pass into the second section's
/// low-pass, whose memories feed back on the next sample; the output is a normalised by the processed resonance. The
/// morph moves each sample's cutoff with the input itself.
///
/// **Modes.** A mode is the saturation (@ref Saturation), chosen per block by the processed resonance: BYPASS (at most
/// 750000000), TANH (@ref tanh at drive 2, past 750000000) and ANTIALIASED (@ref AntialiasedTanh at drive 1, past
/// 900000000); and the cutoff (@ref Cutoff): FIXED with the morph at zero, when m' is m clamped for the whole block
/// (x·2^4 × 0 rounds to 0 exactly), else MORPHING. All share one State, so a resonance or morph change between blocks
/// carries every memory and the saturator's position.
///
/// **The saturator's position.** BYPASS and TANH keep it tracking the signal with @ref AntialiasedTanh::place, so that
/// switching ANTIALIASED in interpolates from the last sample rather than from a stale position.
///
/// **Reset.** The kernel's reset silences the sections and keeps the saturator's position.
///
/// **Wrapping.** Every sum and difference wraps modulo 2^32, as the target's `add` and `sub` do: at full scale the
/// input less its low-pass and the loop's sum leave q31.
struct HighPassLadder {
	using value_type = q31; ///< A sample, Q31.
	using step_type = q31;  ///< A sample, Q31: one step per sample.

	/// @brief The saturation the loop runs, chosen per block by the processed resonance.
	enum class Saturation : uint8_t {
		BYPASS,      ///< No saturation; the saturator's position tracks the signal.
		TANH,        ///< @ref tanh at drive 2; the saturator's position tracks the signal.
		ANTIALIASED, ///< @ref AntialiasedTanh at drive 1.
	};

	/// @brief How each sample's cutoff is found.
	enum class Cutoff : uint8_t {
		FIXED,    ///< The morph is zero: the block's moveability, clamped, for every sample.
		MORPHING, ///< The block's moveability nudged by each sample's input times the morph, then clamped.
	};

	/// @brief A saturation and a cutoff: the block's mode.
	enum class Mode : uint8_t {
		BYPASS,
		BYPASS_MORPHING,
		TANH,
		TANH_MORPHING,
		ANTIALIASED,
		ANTIALIASED_MORPHING,
	};
	/// All share one State.
	using Modes = ModeList<Mode::BYPASS, Mode::BYPASS_MORPHING, Mode::TANH, Mode::TANH_MORPHING, Mode::ANTIALIASED,
	                       Mode::ANTIALIASED_MORPHING>;

	/// @brief The mode running @p saturation with @p cutoff.
	[[nodiscard]] static constexpr Mode mode_of(Saturation saturation, Cutoff cutoff) {
		return static_cast<Mode>(static_cast<uint8_t>(saturation) * 2 + static_cast<uint8_t>(cutoff));
	}
	/// @brief The saturation mode @p m runs.
	[[nodiscard]] static constexpr Saturation saturation_of(Mode m) {
		return static_cast<Saturation>(static_cast<uint8_t>(m) / 2);
	}
	/// @brief The cutoff mode @p m runs.
	[[nodiscard]] static constexpr Cutoff cutoff_of(Mode m) { return static_cast<Cutoff>(static_cast<uint8_t>(m) % 2); }

	/// @brief What the owner sets each block.
	struct Settings {
		q31 gain = 0; ///< The volume going in, Q31.
		/// The cutoff (Q31, lifted by 2^5 onto the tan table: see @ref FilterConfig::frequency), the resonance (Q29)
		/// and the morph (Q29). The oversampling flag is not read.
		FilterConfig config{};
	};

	/// @brief What a step reads, and the gain compensation the owner reads back.
	struct Coefficients {
		q31 moveability = 0;         ///< tan(f) / (1 + tan(f)), Q31.
		q31 morph = 0;               ///< The morph, Q31.
		q31 processed_resonance = 0; ///< The resonance as fed back, Q30 (1 is 1073741824), the extra feedback applied.
		/// The output normalisation: 2^31 over the processed resonance's top 8 bits (its value >> 23).
		q31 divide_by_processed_resonance = 0;
		q31 low_pass_1_feedback = 0;         ///< The second section's feedback, Q30.
		q31 high_pass_3_feedback = 0;        ///< The third section's feedback, Q30.
		q31 divide_by_total_moveability = 0; ///< 1 / (1 − the loop gain), Q28 (1 is 268435456).
		q31 gain_compensation = 0;           ///< The gain going in, cut as the resonance rises, Q31.
		Mode mode = Mode::BYPASS;            ///< The saturation and cutoff, dispatched on once per block.
	};

	/// @brief One channel's ladder.
	/// @details State{} is the filter set's zeroed ladder: every memory 0 and the saturator's position 0, the bottom of
	/// the curve rather than its centre, so an ANTIALIASED first block interpolates its first sample from there.
	struct State {
		OnePole::State high_pass_1{};                    ///< The first section, stepped as a high-pass.
		OnePole::State low_pass_1{};                     ///< The second section, stepped as a low-pass.
		OnePole::State high_pass_3{};                    ///< The third section, stepped as a high-pass.
		AntialiasedTanh::State saturator{.position = 0}; ///< The saturator's last position.
	};

	/// @brief The reset: the sections silenced, the saturator's position kept.
	static void reset(State& s) {
		s.high_pass_1 = {};
		s.low_pass_1 = {};
		s.high_pass_3 = {};
	}

	/// @brief The coefficients for @p s.
	/// @details The resonance is squared onto a curve, floored, given extra feedback (× 1200000000 / 2^31 × 2) and
	/// inverted for the output's normalisation; the loop gain's reciprocal is taken through doubles. The gain falls
	/// with the fourth power of the resonance.
	[[nodiscard]] [[gnu::always_inline]] static Coefficients coefficients(const Settings& s) {
		constexpr int32_t kExtraFeedback = 1200000000;
		constexpr int32_t kResonanceUpperLimit = 536870911;
		constexpr int32_t kLeastResonance = 134217728;

		Coefficients c{};
		c.morph = lshift_and_saturate<2>(s.config.morph);
		const CurvedFrequency curved = curve_frequency(s.config.frequency);
		c.moveability = curved.fc;

		int32_t resonance = kOneQ31 - (std::min(s.config.resonance, kResonanceUpperLimit) << 2);
		resonance = multiply_32x32_rshift32_rounded(resonance, resonance) << 1;
		const int32_t processed_resonance_unaltered = std::max(kOneQ31 - resonance, kLeastResonance);

		c.processed_resonance = multiply_32x32_rshift32(processed_resonance_unaltered, kExtraFeedback) << 1;
		c.divide_by_processed_resonance =
		    static_cast<q31>(2147483648.0 / static_cast<double>(c.processed_resonance >> 23));

		// Q29 (1 is 536870912).
		const int32_t moveability_times_resonance =
		    multiply_32x32_rshift32(processed_resonance_unaltered, c.moveability);
		// Q28 (1 is 268435456).
		const int32_t moveability_squared_times_resonance =
		    multiply_32x32_rshift32(moveability_times_resonance, c.moveability);

		c.high_pass_3_feedback = -multiply_32x32_rshift32_rounded(c.moveability, curved.divide_by_one_plus_tanned);
		c.low_pass_1_feedback = curved.divide_by_one_plus_tanned >> 1;

		const auto to_divide_by = static_cast<uint32_t>(int32_t{268435456} - (moveability_times_resonance >> 1)
		                                                + moveability_squared_times_resonance);
		c.divide_by_total_moveability = static_cast<int32_t>(static_cast<double>(c.processed_resonance) * 67108864.0
		                                                     / static_cast<double>(to_divide_by));

		Saturation saturation = Saturation::BYPASS;
		if (c.processed_resonance > kAntialiasAbove) {
			saturation = Saturation::ANTIALIASED;
		}
		else if (c.processed_resonance > kTanhAbove) {
			saturation = Saturation::TANH;
		}
		c.mode = mode_of(saturation, c.morph == 0 ? Cutoff::FIXED : Cutoff::MORPHING);

		// The volume falls as the resonance rises.
		const q31 raw_resonance = std::min(s.config.resonance, q31{kOneQ31 >> 2}) << 2;
		q31 squared = multiply_32x32_rshift32(raw_resonance, raw_resonance) << 1;
		squared = (multiply_32x32_rshift32(squared, squared) >> 4) * 19;
		c.gain_compensation = multiply_32x32_rshift32(s.gain, kOneQ31 - squared) << 1;
		return c;
	}

	/// @brief The gain compensation, Q31: the gain going in, cut as the resonance rises.
	[[nodiscard]] [[gnu::always_inline]] static constexpr q31 gain(const Coefficients& c) {
		return c.gain_compensation;
	}

	/// @brief One sample through the ladder.
	/// @tparam M The block's saturation and cutoff.
	/// @param c The block's coefficients.
	/// @param s The channel's sections and saturator position, updated.
	/// @param x The input, Q31.
	/// @return The ladder's output, Q31.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static q31 step(const Coefficients& c, const EmptyBlock& /*b*/, State& s,
	                                                     q31 x) {
		constexpr Saturation kSaturation = saturation_of(M);
		// The inputs are small (about 16 bits), so this nudge is too; the floor is about as low as the cutoff goes
		// without sounding broken. A ternary, as `std::max` is an out-of-line call where the app instantiates this.
		// FIXED: the nudge by a zero morph is exactly zero, so the clamp of the block's moveability is the same.
		const q31 nudged = cutoff_of(M) == Cutoff::FIXED
		                       ? c.moveability
		                       : multiply_accumulate_32x32_rshift32_rounded(c.moveability, x << 4, c.morph);
		const q31 m = nudged < kLowerLimit ? kLowerLimit : nudged;

		const q31 first_output = section_step<OnePole::Mode::HIGH_PASS>(s.high_pass_1, m, x);
		const q31 feedbacks = wrapping_add(OnePole::feedback(s.high_pass_3, c.high_pass_3_feedback),
		                                   OnePole::feedback(s.low_pass_1, c.low_pass_1_feedback));
		q31 a = multiply_32x32_rshift32_rounded(c.divide_by_total_moveability, wrapping_add(first_output, feedbacks))
		        << kLoopShift;

		if constexpr (kSaturation == Saturation::ANTIALIASED) {
			a = AntialiasedTanh::step(kSaturator, EmptyBlock{}, s.saturator, a);
		}
		else {
			// The saturator is placed at shift 2 although it saturates at drive 1 (shift 1): this placement is
			// part of the output contract, so switching ANTIALIASED in interpolates its first sample from a position
			// twice as far from the centre (short of saturation).
			AntialiasedTanh::place(s.saturator,
			                       static_cast<uint32_t>(lshift_and_saturate<kPlaceShift>(a)) + kTanhCentre);
			if constexpr (kSaturation == Saturation::TANH) {
				a = tanh<kTanhDrive>(a);
			}
		}

		// Only the sections' memories are wanted here: they feed back on the next sample.
		std::ignore = section_step<OnePole::Mode::LOW_PASS>(
		    s.low_pass_1, m, section_step<OnePole::Mode::HIGH_PASS>(s.high_pass_3, m, a));

		return multiply_32x32_rshift32_rounded(a, c.divide_by_processed_resonance) << kOutputShift;
	}

private:
	/// @brief The processed resonance (Q30) above which the loop saturates through the antialiased tanh.
	static constexpr q31 kAntialiasAbove = 900000000;
	/// @brief The processed resonance (Q30) above which the loop saturates through the tanh.
	static constexpr q31 kTanhAbove = 750000000;
	/// @brief The lowest per-sample moveability, Q31: −2^23.
	static constexpr q31 kLowerLimit = -(kOneQ31 >> 8);
	/// @brief The shift from the loop's Q28 reciprocal product back to Q31, doubled.
	static constexpr int kLoopShift = 4 + 1;
	/// @brief The shift from the output normalisation's product back to Q31, halved.
	static constexpr int kOutputShift = 8 - 1;
	/// @brief The tanh's drive, as a power of two.
	static constexpr uint32_t kTanhDrive = 2;
	/// @brief The shift the bypass and the tanh place the saturator's position at.
	static constexpr int kPlaceShift = 2;
	/// @brief The antialiased tanh at drive 1.
	static constexpr AntialiasedTanh::Coefficients kSaturator = AntialiasedTanh::coefficients({.drive = 1});

	/// @brief One step of @p section at moveability @p m in mode @p M.
	template <OnePole::Mode M>
	[[nodiscard]] [[gnu::always_inline]] static q31 section_step(OnePole::State& section, q31 m, q31 x) {
		return OnePole::step<M>(OnePole::Coefficients{.m = m, .mode = M}, EmptyBlock{}, section, x);
	}
};

static_assert(processor_kernel<HighPassLadder>);
static_assert(!std::is_polymorphic_v<HighPassLadder>);

} // namespace deluge::dsp::fixed
