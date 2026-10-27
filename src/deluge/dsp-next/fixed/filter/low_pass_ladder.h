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
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/curves/quick_log.h"
#include "dsp-next/fixed/filter/filter_modes.h"
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp-next/fixed/filter/shift_tracker.h"
#include "dsp-next/fixed/interpolation/table_interpolation.h"
#include "dsp-next/fixed/shaping/tanh.h"
#include "util/fixedpoint.h"
#include "util/param_functions.h" // lshiftAndSaturate

#include <algorithm>
#include <array>
#include <cstdint>
#include <tuple>
#include <type_traits>

/// @file
/// @brief LowPassLadder: the transistor-ladder low-pass as a processor kernel, its noise a policy.
namespace deluge::dsp::fixed {

namespace low_pass_ladder_detail {

/// The drive ladder's resonance (Q30) above which it oversamples, by log cutoff: 2^24 per entry of @ref quick_log's
/// scale, 6 bits of table, each entry at 2^-16 of the threshold.
inline constexpr std::array<int16_t, 65> kResonanceThresholdsForOversampling = {
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    15500, 20735, 17000, 9000,  9000,  9000,  9000,  9000,  9000,  9000,  9000,  9000,  9000,
};

/// The oversampled drive ladder's highest resonance (Q30), on the same scale as
/// @ref kResonanceThresholdsForOversampling: it would feed back too hard near the top.
inline constexpr std::array<int16_t, 65> kResonanceLimits = {
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
    28415, 20000, 17000, 17000, 17000, 17000, 17000, 17000, 17000, 17000, 17000, 17000, 17000,
};

} // namespace low_pass_ladder_detail

/// @brief The transistor-ladder low-pass: the half ladder (12dB), the full ladder (24dB), and the driven full ladder,
/// at the rate or oversampled.
/// @details Per sample, from the four one-pole sections' memories z₀…z₃ (@ref OnePole, Q31):
///
///     m = moveability × (1 + n),   x = (input − R × Σ fᵢ zᵢ) × D,   then x through the sections at m
///
/// where n is the channel's noise follower (@ref ShiftTracker at shift 7 over a quarter of each noise draw), R the
/// processed resonance, fᵢ the feedbacks and D the reciprocal of the loop gain. When the morph is up or the resonance
/// high, x is saturated (@ref tanh at drive 2) with the morph's share of the input added. The 12dB ladder runs two
/// low-passes and an all-pass; the 24dB ladder four low-passes. The drive ladder saturates its feedback sum (drive 7)
/// and its output (drive 4); oversampled, it steps twice over the held input and drops the first output.
///
/// **Modes.** A mode is a ladder (@ref Ladder: T12, T24, DRIVE, DRIVE_OVERSAMPLED) and how its input is formed
/// (@ref Input), both fixed for the block, so the loop branches on neither; the oversampled drive ladder only ever
/// saturates its input, so it has that one form. Every mode shares one State, so a mode
/// change between blocks, oversampling switched on or off included, carries every section's memory and the follower.
/// The input forms are exact specialisations: with the resonance at zero the feedback term is R × Σ = 0 and D is
/// exactly 1 (Q30), so UNRESONANT skips the feedback sum and scales by D alone, giving every bit the general form
/// gives.
///
/// **Noise.** Each step draws one value from @p Noise (`Noise::render()`), two when oversampled. Under `Stereo` the
/// left channel steps first in every frame, so with a shared stream (@ref CongNoise) the left channel draws first.
///
/// **Reset.** The kernel's reset silences the sections and keeps the noise follower: a reset ladder picks up the
/// same wobble it had.
///
/// **Wrapping.** Every sum and difference wraps modulo 2^32, as the target's `add` and `sub` do: at full scale the
/// input less its feedback, and the sections' sums (@ref OnePole), leave q31.
/// @tparam Noise A policy with `static q31 render()`, each call the next draw.
template <class Noise>
struct LowPassLadder {
	using value_type = q31; ///< A sample, Q31.
	using step_type = q31;  ///< A sample, Q31: one step per sample.

	/// @brief Which ladder runs.
	enum class Ladder : uint8_t {
		T12,               ///< The half ladder: two low-passes and an all-pass.
		T24,               ///< The full ladder: four low-passes.
		DRIVE,             ///< The full ladder with its feedback and output saturated.
		DRIVE_OVERSAMPLED, ///< DRIVE stepped twice per sample over the held input, the first output dropped.
	};

	/// @brief How the input going into the sections is formed.
	enum class Input : uint8_t {
		SATURATING, ///< Less its feedback and normalised, then saturated with the morph's share added: morph up or
		            ///< resonance high.
		RESONANT,   ///< Less its feedback and normalised.
		UNRESONANT, ///< Normalised: the resonance is zero, so nothing is fed back and the normaliser is exactly one.
	};

	/// @brief A ladder and its input form: the block's mode.
	enum class Mode : uint8_t {
		T12_SATURATING,
		T12,
		T12_UNRESONANT,
		T24_SATURATING,
		T24,
		T24_UNRESONANT,
		DRIVE_SATURATING,
		DRIVE,
		DRIVE_UNRESONANT,
		/// The oversampled drive ladder, SATURATING: it oversamples only at a resonance past the input's saturation
		/// threshold, so it has no other input form.
		DRIVE_OVERSAMPLED_SATURATING,
	};
	/// All share one State.
	using Modes = ModeList<Mode::T12_SATURATING, Mode::T12, Mode::T12_UNRESONANT, Mode::T24_SATURATING, Mode::T24,
	                       Mode::T24_UNRESONANT, Mode::DRIVE_SATURATING, Mode::DRIVE, Mode::DRIVE_UNRESONANT,
	                       Mode::DRIVE_OVERSAMPLED_SATURATING>;

	/// @brief The mode running @p ladder with input form @p input; DRIVE_OVERSAMPLED takes SATURATING only.
	[[nodiscard]] static constexpr Mode mode_of(Ladder ladder, Input input) {
		return static_cast<Mode>(static_cast<uint8_t>(ladder) * 3 + static_cast<uint8_t>(input));
	}
	/// @brief The ladder mode @p m runs.
	[[nodiscard]] static constexpr Ladder ladder_of(Mode m) { return static_cast<Ladder>(static_cast<uint8_t>(m) / 3); }
	/// @brief The input form mode @p m runs.
	[[nodiscard]] static constexpr Input input_of(Mode m) { return static_cast<Input>(static_cast<uint8_t>(m) % 3); }

	/// @brief What the owner sets each block.
	struct Settings {
		q31 gain = 0; ///< The volume going in, Q31.
		/// The cutoff (Q31, lifted by 2^5 onto the tan table: see @ref FilterConfig::frequency), the resonance (Q29)
		/// and the morph (Q29), and whether the drive ladder may oversample.
		FilterConfig config{};
		LowPassLadderMode mode = LowPassLadderMode::TRANSISTOR_12DB; ///< The slot's ladder.
	};

	/// @brief What a step reads, and the gain compensation the owner reads back.
	struct Coefficients {
		q31 moveability = 0;         ///< tan(f) / (1 + tan(f)), Q31, held at least 4317840.
		q31 morph = 0;               ///< The morph, Q31.
		q31 processed_resonance = 0; ///< The resonance as fed back, Q30 (1 is 1073741824).
		/// 1 / (1 + the loop gain through the sections and resonance), Q30.
		q31 divide_by_total_moveability_and_processed_resonance = 0;
		q31 divide_by_one_plus_tanned = 0; ///< 1 / (1 + tan(f)), Q31: the last section's feedback.
		/// The first three sections' feedbacks, Q30; the 12dB ladder reads the first two.
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): indexing is no call
		q31 feedbacks[3]{};
		q31 gain_compensation = 0; ///< The gain going in, compensated for the resonance and mode, Q31.
		Mode mode = Mode::T12;     ///< The ladder and its input form, dispatched on once per block.
	};

	/// @brief The noise follower: a one-pole at shift 7 over a quarter of each draw.
	using Follower = ShiftTracker<7>;

	/// @brief One channel's ladder. State{} is silence with the follower at rest.
	struct State {
		Follower::State noise{}; ///< The low-passed noise, Q31 at a quarter of the noise's range.
		/// The four sections, first to last; the 12dB ladder runs the first three.
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): indexing is no call
		OnePole::State sections[4]{};
	};

	/// @brief The reset: every section silenced, the noise follower kept.
	static void reset(State& s) {
		for (OnePole::State& section : s.sections) {
			section = {};
		}
	}

	/// @brief The coefficients for @p s.
	/// @details The 12dB and 24dB ladders lose resonance as the cutoff falls very low, and gain extra feedback while
	/// the cutoff is low enough not to alias; their gain is compensated for the resonance. The drive ladder oversamples
	/// when allowed and when its resonance passes a threshold that falls with the cutoff (from a log cutoff of 52 on
	/// @ref quick_log's scale); oversampled, its cutoff halves, is pulled back for the shift oversampling makes and
	/// held below 39056384, and its resonance is limited. Its gain is 0.8 of the gain going in.
	[[nodiscard]] [[gnu::always_inline]] static Coefficients coefficients(const Settings& s) {
		Coefficients c{};
		c.morph = lshiftAndSaturate<2>(s.config.morph);
		q31 frequency = s.config.frequency;
		bool oversample = false;
		Ladder ladder = Ladder::T12;

		if (s.mode == LowPassLadderMode::TRANSISTOR_24DB_DRIVE) {
			const int32_t resonance = ONE_Q31 - (s.config.resonance << 2);
			c.processed_resonance = ONE_Q31 - resonance;

			int32_t log_frequency = quick_log(static_cast<uint32_t>(frequency));
			log_frequency = std::min(log_frequency, int32_t{63} << 24);

			if (s.config.allow_oversampling && (log_frequency >> 24) > 51) {
				constexpr const int16_t* kThresholds =
				    low_pass_ladder_detail::kResonanceThresholdsForOversampling.data();
				const int32_t threshold = linear_table_interpolator.interpolateSigned(
				    static_cast<uint32_t>(log_frequency), 30, kThresholds, 6);
				oversample = c.processed_resonance > threshold;
			}

			if (oversample) {
				frequency >>= 1;
				log_frequency -= 33554432;
				frequency -= (multiply_32x32_rshift32_rounded(log_frequency, frequency) >> 8) * 34;
				frequency = std::min(int32_t{39056384}, frequency);
				constexpr const int16_t* kLimits = low_pass_ladder_detail::kResonanceLimits.data();
				const int32_t limit =
				    linear_table_interpolator.interpolateSigned(static_cast<uint32_t>(log_frequency), 30, kLimits, 6);
				c.processed_resonance = std::min(c.processed_resonance, limit);
			}
			ladder = oversample ? Ladder::DRIVE_OVERSAMPLED : Ladder::DRIVE;
		}
		else {
			constexpr int32_t kHowMuchToKeep = ONE_Q31 - 33;
			constexpr int32_t kResonanceUpperLimit = 510000000; // above this it feeds back lots
			int32_t resonance = ONE_Q31 - (std::min(s.config.resonance, kResonanceUpperLimit) << 2);
			resonance = multiply_32x32_rshift32_rounded(resonance, resonance) << 1;
			c.processed_resonance = ONE_Q31 - resonance;
			c.processed_resonance = multiply_32x32_rshift32_rounded(c.processed_resonance, kHowMuchToKeep) << 1;
			ladder = s.mode == LowPassLadderMode::TRANSISTOR_12DB ? Ladder::T12 : Ladder::T24;
		}

		const CurvedFrequency curved = curve_frequency(frequency);
		c.divide_by_one_plus_tanned = curved.divide_by_one_plus_tanned;
		c.moveability = std::max(curved.fc, q31{4317840});

		if (ladder == Ladder::T12) {
			// Between -2 and 0, Q30.
			const int32_t moveability_negative = c.moveability - 1073741824;
			c.feedbacks[1] = multiply_32x32_rshift32_rounded(moveability_negative, c.divide_by_one_plus_tanned) << 1;
			c.feedbacks[0] = multiply_32x32_rshift32_rounded(c.feedbacks[1], c.moveability) << 1;
			c.divide_by_total_moveability_and_processed_resonance = static_cast<q31>(
			    int64_t{67108864} * 1073741824
			    / (67108864
			       + multiply_32x32_rshift32_rounded(
			           c.processed_resonance,
			           multiply_32x32_rshift32_rounded(
			               moveability_negative, multiply_32x32_rshift32_rounded(c.moveability, c.moveability)))));
		}
		else {
			c.feedbacks[2] = multiply_32x32_rshift32_rounded(c.divide_by_one_plus_tanned, c.moveability);
			c.feedbacks[1] = multiply_32x32_rshift32_rounded(c.feedbacks[2], c.moveability) << 1;
			c.feedbacks[0] = multiply_32x32_rshift32_rounded(c.feedbacks[1], c.moveability) << 1;

			// Q26 (1 is 67108864).
			const int32_t one_plus_loop_gain =
			    67108864
			    + multiply_32x32_rshift32_rounded(
			        c.moveability,
			        multiply_32x32_rshift32_rounded(
			            c.moveability,
			            multiply_32x32_rshift32_rounded(
			                c.moveability, multiply_32x32_rshift32_rounded(c.moveability, c.processed_resonance))));
			c.divide_by_total_moveability_and_processed_resonance =
			    static_cast<q31>(72057594037927936.0 / static_cast<double>(one_plus_loop_gain));
		}

		q31 gain = s.gain;
		if (s.mode != LowPassLadderMode::TRANSISTOR_24DB_DRIVE) {
			// Extra feedback, but only while the cutoff is low enough not to alias.
			if (curved.tanned <= 304587486) {
				c.processed_resonance = multiply_32x32_rshift32_rounded(c.processed_resonance, 1150000000) << 1;
			}
			else {
				c.processed_resonance >>= 1;
			}

			int32_t cut = std::min(s.config.resonance, int32_t{536870911});
			cut = 536870912 - cut;
			cut = multiply_32x32_rshift32(cut, cut) << 3;
			cut = 536870912 - cut;
			const int32_t gain_modifier = 268435456 + cut;
			gain = multiply_32x32_rshift32(gain, gain_modifier) << 3;
		}
		else {
			// The drive ladder's output is louder.
			gain = static_cast<q31>(gain * 0.8);
		}
		c.gain_compensation = gain;

		Input input = Input::RESONANT;
		// An oversampling drive ladder's resonance is past its threshold, so this holds for it (see
		// kLowestOversamplingResonance).
		if (c.morph > 0 || c.processed_resonance > kSaturateAbove) {
			input = Input::SATURATING;
		}
		else if (c.processed_resonance == 0 && c.divide_by_total_moveability_and_processed_resonance == kUnity) {
			input = Input::UNRESONANT;
		}
		c.mode = mode_of(ladder, input);
		return c;
	}

	/// @brief The gain compensation, Q31: the gain going in, compensated for the resonance and mode.
	[[nodiscard]] [[gnu::always_inline]] static constexpr q31 gain(const Coefficients& c) {
		return c.gain_compensation;
	}

	/// @brief One sample through the mode's ladder.
	/// @tparam M The block's mode.
	/// @param c The block's coefficients.
	/// @param s The channel's follower and sections, updated.
	/// @param x The input, Q31.
	/// @return The ladder's output, Q31.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static q31 step(const Coefficients& c, const EmptyBlock& /*b*/, State& s,
	                                                     q31 x) {
		constexpr Ladder kLadder = ladder_of(M);
		constexpr Input kInput = input_of(M);
		if constexpr (kLadder == Ladder::T12) {
			return half_ladder<kInput>(c, s, x);
		}
		else if constexpr (kLadder == Ladder::T24) {
			return full_ladder<kInput>(c, s, x);
		}
		else {
			if constexpr (kLadder == Ladder::DRIVE_OVERSAMPLED) {
				// The input held for both halves of the sample works better than interpolating it; the first output
				// is dropped (the ladder itself filters what that crude downsampling would alias).
				std::ignore = drive_ladder<kInput>(c, s, x);
			}
			return tanh<kOutputDrive>(drive_ladder<kInput>(c, s, x));
		}
	}

private:
	/// @brief The resonance (Q30) above which the input is saturated even with the morph at zero.
	static constexpr q31 kSaturateAbove = 510000000;
	/// @brief One in Q30: the loop-gain normaliser with nothing fed back.
	static constexpr q31 kUnity = q31{1} << 30;
	/// @brief The lowest resonance (Q30) the drive ladder oversamples above: its threshold table's least entry, which
	/// the table holds at 2^-16 of the threshold, and any blend of two entries is at least that.
	static constexpr q31 kLowestOversamplingResonance =
	    q31{*std::ranges::min_element(low_pass_ladder_detail::kResonanceThresholdsForOversampling)} << 16;
	static_assert(kLowestOversamplingResonance > kSaturateAbove,
	              "an oversampling drive ladder must always saturate its input: DRIVE_OVERSAMPLED has no other mode");
	/// @brief The input saturation's drive, as a power of two.
	static constexpr uint32_t kInputDrive = 2;
	/// @brief The drive ladder's feedback saturation drive, as a power of two.
	static constexpr uint32_t kFeedbackDrive = 7;
	/// @brief The drive ladder's output saturation drive, as a power of two.
	static constexpr uint32_t kOutputDrive = 4;

	/// @brief This sample's moveability, Q31: the block's nudged by the channel's follower after one noise draw.
	[[nodiscard]] [[gnu::always_inline]] static q31 noisy_moveability(const Coefficients& c, State& s) {
		const q31 follower = Follower::template step<Follower::Mode::TRACK>(Follower::Coefficients{}, EmptyBlock{},
		                                                                    s.noise, Noise::render() >> 2);
		return wrapping_add(c.moveability, multiply_32x32_rshift32(c.moveability, follower));
	}

	/// @brief The input less the scaled feedback, normalised, Q31; saturated with the morph's share of the input
	/// added in SATURATING.
	/// @tparam I The block's input form; UNRESONANT reads neither @p feedback_sum nor the resonance.
	/// @param feedback_sum Computes the sections' feedback (Q31), called unless UNRESONANT.
	template <Input I, class FeedbackSum>
	[[nodiscard]] [[gnu::always_inline]] static q31 scale_input(const Coefficients& c, q31 x,
	                                                            FeedbackSum&& feedback_sum) {
		if constexpr (I == Input::UNRESONANT) {
			// The general form with R = 0 and D = 1 (Q30): nothing fed back, and x × 2^30 rounded off its low 32 bits
			// is (x + 2) >> 2, which the << 2 makes (x + 2) with its low two bits cleared, modulo 2^32.
			return static_cast<q31>((static_cast<uint32_t>(x) + 2u) & ~3u);
		}
		else {
			const q31 fed_back = multiply_32x32_rshift32_rounded(feedback_sum(), c.processed_resonance) << 3;
			const q31 scaled = multiply_32x32_rshift32_rounded(wrapping_sub(x, fed_back),
			                                                   c.divide_by_total_moveability_and_processed_resonance)
			                   << 2;
			if constexpr (I == Input::SATURATING) {
				const q31 extra = multiply_32x32_rshift32(x, c.morph);
				return tanh<kInputDrive>(wrapping_add(scaled, wrapping_add(extra, extra)));
			}
			else {
				return scaled;
			}
		}
	}

	/// @brief One step of @p section at moveability @p m in mode @p M.
	template <OnePole::Mode M>
	[[nodiscard]] [[gnu::always_inline]] static q31 section_step(OnePole::State& section, q31 m, q31 x) {
		return OnePole::step<M>(OnePole::Coefficients{.m = m, .mode = M}, EmptyBlock{}, section, x);
	}

	/// @brief The four sections' feedback at a quarter scale, summed, then restored, Q31.
	/// @details The last section's feedback "should" be halved onto Q30, but the ladder sounds better (and reaches
	/// full resonance) without.
	[[nodiscard]] [[gnu::always_inline]] static q31 full_feedback(const Coefficients& c, const State& s) {
		const q31 sum =
		    wrapping_add(wrapping_add(OnePole::feedback_unshifted(s.sections[0], c.feedbacks[0]),
		                              OnePole::feedback_unshifted(s.sections[1], c.feedbacks[1])),
		                 wrapping_add(OnePole::feedback_unshifted(s.sections[2], c.feedbacks[2]),
		                              OnePole::feedback_unshifted(s.sections[3], c.divide_by_one_plus_tanned)));
		return sum << 2;
	}

	/// @brief One sample (Q31) through the half ladder, its input formed as @p I.
	template <Input I>
	[[nodiscard]] [[gnu::always_inline]] static q31 half_ladder(const Coefficients& c, State& s, q31 input) {
		const q31 m = noisy_moveability(c, s);
		const q31 x = scale_input<I>(c, input, [&] [[gnu::always_inline]] {
			return wrapping_add(wrapping_add(OnePole::feedback(s.sections[0], c.feedbacks[0]),
			                                 OnePole::feedback(s.sections[1], c.feedbacks[1])),
			                    OnePole::feedback(s.sections[2], c.divide_by_one_plus_tanned));
		});
		const q31 a = section_step<OnePole::Mode::LOW_PASS>(s.sections[0], m, x);
		const q31 b = section_step<OnePole::Mode::LOW_PASS>(s.sections[1], m, a);
		return section_step<OnePole::Mode::ALL_PASS>(s.sections[2], m, b) << 1;
	}

	/// @brief One sample (Q31) through the full ladder, its input formed as @p I.
	template <Input I>
	[[nodiscard]] [[gnu::always_inline]] static q31 full_ladder(const Coefficients& c, State& s, q31 input) {
		const q31 m = noisy_moveability(c, s);
		const q31 x = scale_input<I>(c, input, [&] [[gnu::always_inline]] { return full_feedback(c, s); });
		return four_sections(s, m, x) << 1;
	}

	/// @brief One step (Q31) of the drive ladder, its feedback saturated (the input is not, as that is where it would
	/// alias most); the output is not yet saturated. The input is formed as @p I.
	template <Input I>
	[[nodiscard]] [[gnu::always_inline]] static q31 drive_ladder(const Coefficients& c, State& s, q31 input) {
		const q31 m = noisy_moveability(c, s);
		const q31 x =
		    scale_input<I>(c, input, [&] [[gnu::always_inline]] { return tanh<kFeedbackDrive>(full_feedback(c, s)); });
		return four_sections(s, m, x) << 1;
	}

	/// @brief @p x through the four sections' low-passes in turn at moveability @p m.
	[[nodiscard]] [[gnu::always_inline]] static q31 four_sections(State& s, q31 m, q31 x) {
		const q31 a = section_step<OnePole::Mode::LOW_PASS>(s.sections[0], m, x);
		const q31 b = section_step<OnePole::Mode::LOW_PASS>(s.sections[1], m, a);
		const q31 d = section_step<OnePole::Mode::LOW_PASS>(s.sections[2], m, b);
		return section_step<OnePole::Mode::LOW_PASS>(s.sections[3], m, d);
	}
};

} // namespace deluge::dsp::fixed
