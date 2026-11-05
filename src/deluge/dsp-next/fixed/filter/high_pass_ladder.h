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
#include "dsp-next/core/q31_ops.h" // lshift_and_saturate
#include "dsp-next/core/types.h"
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
///     m' = max(m + 8x × morph, −2^-8),   h = HP₁(x),   a = 2 (h + fb₃ z₃ + fb₂ z₂) × D
///
/// where HP₁ steps the first section at m' as a high-pass (@ref OnePole::Mode::HIGH_PASS: x less its low-pass), z₃
/// and z₂ are the third and second sections' memories, fb₃ and fb₂ their feedbacks, and D the reciprocal of the loop
/// gain. a is saturated by the block's mode, then fed through the third section's high-pass into the second section's
/// low-pass, whose memories feed back on the next sample; the output is a / 2R, R the processed resonance. The morph
/// moves each sample's cutoff with the input itself: 8x is x × 2^4 wrapping, so for x outside [−2^27, 2^27) (a loud
/// input) the nudge wraps, and the cutoff jumps.
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
	using value_type = Q<31>; ///< A sample.
	using step_type = Q<31>;  ///< A sample: one step per sample.

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
		int32_t gain = 0; ///< The volume going in, in the owner's format.
		/// The cutoff (Q31, lifted by 2^5 onto the tan table: see @ref FilterConfig::frequency), the resonance (Q29)
		/// and the morph (Q29). The oversampling flag is not read.
		FilterConfig config{};
	};

	/// @brief What a step reads, and the gain compensation the owner reads back.
	struct Coefficients {
		Q<31> moveability{};         ///< tan(f) / (1 + tan(f)), Q31.
		Q<31> morph{};               ///< The morph, Q31.
		Q<30> processed_resonance{}; ///< The resonance as fed back, Q30, the extra feedback applied.
		/// The output normalisation, 1 / 2R at Q25: 2^31 over the processed resonance's top 8 bits (its value >> 23).
		Q<25> divide_by_processed_resonance{};
		Q<30> low_pass_1_feedback{};         ///< The second section's feedback, Q30.
		Q<30> high_pass_3_feedback{};        ///< The third section's feedback, Q30.
		Q<28> divide_by_total_moveability{}; ///< R / (1 − mR/2 + m²R), Q28: R over the loop's denominator.
		int32_t gain_compensation = 0;       ///< The gain going in, cut as the resonance rises, in its format.
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
	/// @details The resonance is squared onto a curve, floored, given extra feedback (× 1200000000 / 2^31) and
	/// inverted for the output's normalisation; the loop gain's reciprocal is taken through doubles. The gain falls
	/// with the fourth power of the resonance.
	[[nodiscard]] [[gnu::always_inline]] static Coefficients coefficients(const Settings& s) {
		constexpr auto kExtraFeedback = Q<31>::from_raw(1200000000);
		constexpr auto kResonanceUpperLimit = Q<29>::from_raw(536870911);
		constexpr auto kLeastResonance = Q<30>::from_raw(134217728);

		Coefficients c{};
		c.morph = as_clamp_then_shift<Q<31>>(s.config.morph);
		const CurvedFrequency curved = curve_frequency(s.config.frequency);
		const Q<31> m = curved.fc;
		const Q<31> divide_by_one_plus_tanned = curved.divide_by_one_plus_tanned;
		c.moveability = m;

		const Q<31> damping =
		    sub_wrap(Q<31>::max(), as_wrap<Q<31>>(std::min(s.config.resonance, kResonanceUpperLimit)));
		// 1 − damping² at Q31, read as Q30: twice it.
		const Q<30> processed_resonance_unaltered = std::max(
		    reinterpret_bits<Q<30>>(sub_wrap(Q<31>::max(), mul_r_as_wrap<Q<31>>(damping, damping))), kLeastResonance);

		c.processed_resonance = mul_as_wrap<Q<30>>(processed_resonance_unaltered, kExtraFeedback);
		// Raw: a double reciprocal of the resonance's top bits, 2^31 over its value >> 23, gives 1 / 2R at Q25.
		c.divide_by_processed_resonance = Q<25>::from_raw(
		    static_cast<int32_t>(2147483648.0 / static_cast<double>(c.processed_resonance.raw() >> 23)));

		const Q<29> moveability_times_resonance = mul(processed_resonance_unaltered, m);
		const Q<28> moveability_squared_times_resonance = mul(moveability_times_resonance, m);

		c.high_pass_3_feedback = neg_wrap(mul_r(m, divide_by_one_plus_tanned));
		c.low_pass_1_feedback = as_floor<Q<30>>(divide_by_one_plus_tanned);

		const Q<28> to_divide_by = add_wrap(sub_wrap(Q<28>::one(), as_floor<Q<28>>(moveability_times_resonance)),
		                                    moveability_squared_times_resonance);
		// Raw: a double division, R (Q30) × 2^26 over the Q28 denominator, gives Q28.
		c.divide_by_total_moveability =
		    Q<28>::from_raw(static_cast<int32_t>(static_cast<double>(c.processed_resonance.raw()) * 67108864.0
		                                         / static_cast<double>(static_cast<uint32_t>(to_divide_by.raw()))));

		Saturation saturation = Saturation::BYPASS;
		if (c.processed_resonance > kAntialiasAbove) {
			saturation = Saturation::ANTIALIASED;
		}
		else if (c.processed_resonance > kTanhAbove) {
			saturation = Saturation::TANH;
		}
		c.mode = mode_of(saturation, c.morph == Q<31>{} ? Cutoff::FIXED : Cutoff::MORPHING);

		// The volume falls as the resonance rises: by 19/32 of its fourth power.
		const auto resonance = as_wrap<Q<31>>(std::min(s.config.resonance, Q<29>::from_raw(kOneQ31 >> 2)));
		const auto squared = mul_as_wrap<Q<31>>(resonance, resonance);
		const Q<30> fourth = mul(squared, squared);
		// Raw: the × 19 / 2^4 is an integer scale, reading the Q30 fourth power as Q31.
		const auto cut = Q<31>::from_raw((fourth.raw() >> 4) * 19);
		// The gain is read as Q31 only to scale it: the Q31 factor moved back gives the same bits in any format.
		c.gain_compensation = mul_as_wrap<Q<31>>(Q<31>::from_raw(s.gain), sub_wrap(Q<31>::max(), cut)).raw();
		return c;
	}

	/// @brief The gain compensation: the gain going in, in its format, cut as the resonance rises.
	[[nodiscard]] [[gnu::always_inline]] static constexpr int32_t gain(const Coefficients& c) {
		return c.gain_compensation;
	}

	/// @brief One sample through the ladder.
	/// @tparam M The block's saturation and cutoff.
	/// @param c The block's coefficients.
	/// @param s The channel's sections and saturator position, updated.
	/// @param x The input, Q31.
	/// @return The ladder's output, Q31.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static Q<31> step(const Coefficients& c, const EmptyBlock& /*b*/, State& s,
	                                                       Q<31> x) {
		constexpr Saturation kSaturation = saturation_of(M);
		const Q<31> input = x;
		// The floor is about as low as the cutoff goes without sounding broken. FIXED: the nudge by a zero morph is
		// exactly zero, so the clamp of the block's moveability is the same.
		const Q<31> nudged = cutoff_of(M) == Cutoff::FIXED ? c.moveability : nudge(c, input);
		const Q<31> m = std::max(nudged, kLowerLimit);

		const Q<31> first_output = section_step<OnePole::Mode::HIGH_PASS>(s.high_pass_1, m, x);
		const Q<31> feedbacks = add_wrap(OnePole::feedback(s.high_pass_3, c.high_pass_3_feedback),
		                                 OnePole::feedback(s.low_pass_1, c.low_pass_1_feedback));
		// (h + feedback) × D is Q27; to Q31 and doubled, wrapping.
		Q<31> a =
		    scale_shl_wrap<1>(mul_r_as_wrap<Q<31>>(c.divide_by_total_moveability, add_wrap(first_output, feedbacks)));

		if constexpr (kSaturation == Saturation::ANTIALIASED) {
			a = AntialiasedTanh::step(kSaturator, EmptyBlock{}, s.saturator, a);
		}
		else {
			// The saturator is placed at shift 2 although it saturates at drive 1 (shift 1): this placement is
			// part of the output contract, so switching ANTIALIASED in interpolates its first sample from a position
			// twice as far from the centre (short of saturation). The centre's add wraps the position. Raw: the asm
			// saturate holds the loop's schedule; the builtin a typed saturate compiles to is sunk out of the loop,
			// and the A9 ran the rescheduled loop 5–22% slower (filter_hp_*).
			AntialiasedTanh::place(s.saturator,
			                       static_cast<uint32_t>(lshift_and_saturate<kPlaceShift>(a.raw())) + kTanhCentre);
			if constexpr (kSaturation == Saturation::TANH) {
				a = tanh<kTanhDrive>(a);
			}
		}

		// Only the sections' memories are wanted here: they feed back on the next sample.
		std::ignore = section_step<OnePole::Mode::LOW_PASS>(
		    s.low_pass_1, m, section_step<OnePole::Mode::HIGH_PASS>(s.high_pass_3, m, a));

		return mul_r_as_wrap<Q<31>>(a, c.divide_by_processed_resonance);
	}

private:
	/// @brief The processed resonance above which the loop saturates through the antialiased tanh, Q30.
	static constexpr auto kAntialiasAbove = Q<30>::from_raw(900000000);
	/// @brief The processed resonance above which the loop saturates through the tanh, Q30.
	static constexpr auto kTanhAbove = Q<30>::from_raw(750000000);
	/// @brief The lowest per-sample moveability, Q31: −2^-8.
	static constexpr auto kLowerLimit = Q<31>::from_raw(-(kOneQ31 >> 8));
	/// @brief The tanh's drive, as a power of two.
	static constexpr uint32_t kTanhDrive = 2;
	/// @brief The shift the bypass and the tanh place the saturator's position at.
	static constexpr int kPlaceShift = 2;
	/// @brief The antialiased tanh at drive 1.
	static constexpr AntialiasedTanh::Coefficients kSaturator = AntialiasedTanh::coefficients({.drive = 1});

	/// @brief The block's moveability nudged by the input: m + 8x × morph, Q31.
	/// @details x × 2^4 wraps for x outside [−2^27, 2^27). The rounded high word of 16x × morph is Q30; m is read as
	/// Q30 to take it in one multiply-accumulate, and the sum read back as Q31 is m + 8x × morph.
	[[nodiscard]] [[gnu::always_inline]] static Q<31> nudge(const Coefficients& c, Q<31> x) {
		return reinterpret_bits<Q<31>>(mla_r(reinterpret_bits<Q<30>>(c.moveability), scale_shl_wrap<4>(x), c.morph));
	}

	/// @brief One step of @p section at moveability @p m in mode @p M.
	template <OnePole::Mode M>
	[[nodiscard]] [[gnu::always_inline]] static Q<31> section_step(OnePole::State& section, Q<31> m, Q<31> x) {
		return OnePole::step<M>(OnePole::Coefficients{.m = m, .mode = M}, EmptyBlock{}, section, x);
	}
};

static_assert(processor_kernel<HighPassLadder>);
static_assert(!std::is_polymorphic_v<HighPassLadder>);

} // namespace deluge::dsp::fixed
