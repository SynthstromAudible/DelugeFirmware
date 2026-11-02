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
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/curves/quick_log.h"
#include "dsp-next/fixed/filter/filter_modes.h"
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp-next/fixed/filter/shift_tracker.h"
#include "dsp-next/fixed/interpolation/table_interpolation.h"
#include "dsp-next/fixed/noise/cong.h"
#include "dsp-next/fixed/shaping/tanh.h"

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
///     m = moveability × (1 + n/2),   x = (input − 2R × Σ fᵢ zᵢ) × D,   then x through the sections at m
///
/// where n is the channel's noise follower (@ref ShiftTracker at shift 7 over a quarter of each noise draw), R the
/// processed resonance (Q30), fᵢ the feedbacks (Q30; the last section's is 1 / (1 + tan(f)) read as Q30, twice its
/// value) and D the reciprocal of the loop gain (Q30). When the morph is up or the resonance
/// high, x is saturated (@ref tanh at drive 2) with the morph's share of the input added. The 12dB ladder runs two
/// low-passes and an all-pass; the 24dB ladder four low-passes. The drive ladder saturates its feedback sum (drive 7)
/// and its output (drive 4); oversampled, it steps twice over the held input and drops the first output.
///
/// **Modes.** A mode is a ladder (@ref Ladder: T12, T24, DRIVE, DRIVE_OVERSAMPLED) and how its input is formed
/// (@ref Input), both fixed for the block. Every mode shares one State, so a mode change between blocks, oversampling
/// switched on or off included, carries every section's memory and the follower. UNRESONANT is an exact
/// specialisation: with the resonance at zero the feedback term is R × Σ = 0 and D is exactly 1 (Q30), so it skips
/// the feedback sum and scales by D alone, giving every bit FED_BACK gives. The oversampled drive ladder runs only
/// past a resonance threshold, so it is only ever FED_BACK.
///
/// **The saturation test.** FED_BACK tests the morph and the resonance every sample rather than taking them as modes
/// too: on the Cortex-A9, splitting the saturating form from the plain one measured 5–13% slower, as the ladder's
/// multiply chains lost the test that filled their latency and spilled more.
///
/// **Noise.** Each step draws one value from the block's noise stream (its Context), two when oversampled. Under
/// `Stereo` both channels share the one stream, left first in every frame, as the wrapper steps them.
///
/// **Reset.** The kernel's reset silences the sections and keeps the noise follower: a reset ladder picks up the
/// same wobble it had.
///
/// **Wrapping.** Every sum and difference wraps modulo 2^32, as the target's `add` and `sub` do: at full scale the
/// input less its feedback, and the sections' sums (@ref OnePole), leave q31.
/// @tparam Noise The noise stream the block draws from: a @ref noise_stream, each `next()` the next draw.
template <noise_stream Noise = Cong>
struct LowPassLadder {
	using value_type = q31; ///< A sample, Q31.
	using step_type = q31;  ///< A sample, Q31: one step per sample.
	using Context = Noise;  ///< The noise stream the block draws from: one draw per step, two when oversampled.

	/// @brief Which ladder runs.
	enum class Ladder : uint8_t {
		T12,               ///< The half ladder: two low-passes and an all-pass.
		T24,               ///< The full ladder: four low-passes.
		DRIVE,             ///< The full ladder with its feedback and output saturated.
		DRIVE_OVERSAMPLED, ///< DRIVE stepped twice per sample over the held input, the first output dropped.
	};

	/// @brief How the input going into the sections is formed.
	enum class Input : uint8_t {
		FED_BACK,   ///< Less its feedback and normalised; saturated, the morph's share added, when the morph is up
		            ///< or the resonance high (tested each sample).
		UNRESONANT, ///< Normalised: the resonance and morph are zero, so nothing is fed back, the normaliser is
		            ///< exactly one and nothing saturates.
	};

	/// @brief A ladder and its input form: the block's mode.
	enum class Mode : uint8_t {
		T12,
		T12_UNRESONANT,
		T24,
		T24_UNRESONANT,
		DRIVE,
		DRIVE_UNRESONANT,
		/// The oversampled drive ladder, FED_BACK: it oversamples only past a resonance threshold.
		DRIVE_OVERSAMPLED,
	};
	/// All share one State.
	using Modes = ModeList<Mode::T12, Mode::T12_UNRESONANT, Mode::T24, Mode::T24_UNRESONANT, Mode::DRIVE,
	                       Mode::DRIVE_UNRESONANT, Mode::DRIVE_OVERSAMPLED>;

	/// @brief The mode running @p ladder with input form @p input; DRIVE_OVERSAMPLED takes FED_BACK only.
	[[nodiscard]] static constexpr Mode mode_of(Ladder ladder, Input input) {
		return static_cast<Mode>(static_cast<uint8_t>(ladder) * 2 + static_cast<uint8_t>(input));
	}
	/// @brief The ladder mode @p m runs.
	[[nodiscard]] static constexpr Ladder ladder_of(Mode m) { return static_cast<Ladder>(static_cast<uint8_t>(m) / 2); }
	/// @brief The input form mode @p m runs.
	[[nodiscard]] static constexpr Input input_of(Mode m) { return static_cast<Input>(static_cast<uint8_t>(m) % 2); }

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
		Q<31> moveability{};         ///< tan(f) / (1 + tan(f)), Q31, held at least 4317840.
		Q<31> morph{};               ///< The morph, Q31.
		Q<30> processed_resonance{}; ///< The resonance as fed back, Q30.
		/// 1 / (1 + the loop gain through the sections and resonance), Q30.
		Q<30> divide_by_total_moveability_and_processed_resonance{};
		Q<31> divide_by_one_plus_tanned{}; ///< 1 / (1 + tan(f)), Q31: the last section's feedback.
		/// The first three sections' feedbacks, Q30; the 12dB ladder reads the first two.
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): indexing is no call
		Q<30> feedbacks[3]{};
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
		c.morph = as_clamp_then_shift<Q<31>>(Q<29>::from_raw(s.config.morph));
		q31 frequency = s.config.frequency;
		bool oversample = false;
		Ladder ladder = Ladder::T12;

		if (s.mode == LowPassLadderMode::TRANSISTOR_24DB_DRIVE) {
			// Twice the resonance, Q30.
			c.processed_resonance = as_wrap<Q<30>>(scale_shl_wrap<1>(Q<29>::from_raw(s.config.resonance)));

			// Raw: a log cutoff, 2^24 per octave, and the integer table positions read from it.
			int32_t log_frequency = quick_log(static_cast<uint32_t>(frequency));
			log_frequency = std::min(log_frequency, int32_t{63} << 24);

			if (s.config.allow_oversampling && (log_frequency >> 24) > 51) {
				constexpr const int16_t* kThresholds =
				    low_pass_ladder_detail::kResonanceThresholdsForOversampling.data();
				const auto threshold = Q<30>::from_raw(kLinearTableInterpolator.interpolate_signed(
				    static_cast<uint32_t>(log_frequency), 30, kThresholds, 6));
				oversample = c.processed_resonance > threshold;
			}

			if (oversample) {
				frequency >>= 1;
				log_frequency -= 33554432;
				// The pull-back for the shift oversampling makes, fitted: 34 × 2^-8 of log × cutoff at Q23.
				const Q<23> pull = mul_r(Q<24>::from_raw(log_frequency), Q<31>::from_raw(frequency));
				frequency -= (pull.raw() >> 8) * 34;
				frequency = std::min(int32_t{39056384}, frequency);
				constexpr const int16_t* kLimits = low_pass_ladder_detail::kResonanceLimits.data();
				const auto limit = Q<30>::from_raw(
				    kLinearTableInterpolator.interpolate_signed(static_cast<uint32_t>(log_frequency), 30, kLimits, 6));
				c.processed_resonance = std::min(c.processed_resonance, limit);
			}
			ladder = oversample ? Ladder::DRIVE_OVERSAMPLED : Ladder::DRIVE;
		}
		else {
			constexpr auto kHowMuchToKeep = Q<31>::from_raw(Q<31>::max().raw() - 33);
			constexpr int32_t kResonanceUpperLimit = 510000000; // above this it feeds back lots
			const Q<31> damping = sub_wrap(
			    Q<31>::max(), as_wrap<Q<31>>(Q<29>::from_raw(std::min(s.config.resonance, kResonanceUpperLimit))));
			// 1 − damping² at Q31, read as Q30: twice it, as the drive ladder's is twice the resonance.
			const auto fed_back =
			    reinterpret_bits<Q<30>>(sub_wrap(Q<31>::max(), mul_r_as_wrap<Q<31>>(damping, damping)));
			c.processed_resonance = mul_r_as_wrap<Q<30>>(fed_back, kHowMuchToKeep);
			ladder = s.mode == LowPassLadderMode::TRANSISTOR_12DB ? Ladder::T12 : Ladder::T24;
		}

		const CurvedFrequency curved = curve_frequency(frequency);
		c.divide_by_one_plus_tanned = curved.divide_by_one_plus_tanned;
		const Q<31> m = std::max(curved.fc, Q<31>::from_raw(4317840));
		c.moveability = m;

		if (ladder == Ladder::T12) {
			// m read as Q30 is 2m, so this is 2m − 1: in [−1, 0.76) for m in [0, 0.88].
			const Q<30> moveability_negative = sub_wrap(reinterpret_bits<Q<30>>(m), Q<30>::one());
			c.feedbacks[1] = mul_r_as_wrap<Q<30>>(moveability_negative, c.divide_by_one_plus_tanned);
			c.feedbacks[0] = mul_r_as_wrap<Q<30>>(c.feedbacks[1], m);
			const Q<26> loop_gain = mul_r(c.processed_resonance, mul_r(moveability_negative, mul_r(m, m)));
			// Raw: an integer reciprocal, one at Q56 over the Q26 loop gain plus one, gives Q30.
			c.divide_by_total_moveability_and_processed_resonance =
			    Q<30>::from_raw(static_cast<q31>((int64_t{1} << 56) / add_wrap(Q<26>::one(), loop_gain).raw()));
		}
		else {
			c.feedbacks[2] = mul_r(c.divide_by_one_plus_tanned, m);
			c.feedbacks[1] = mul_r_as_wrap<Q<30>>(c.feedbacks[2], m);
			c.feedbacks[0] = mul_r_as_wrap<Q<30>>(c.feedbacks[1], m);

			const Q<26> loop_gain = mul_r(m, mul_r(m, mul_r(m, mul_r(m, c.processed_resonance))));
			const Q<26> one_plus_loop_gain = add_wrap(Q<26>::one(), loop_gain);
			// Raw: a double reciprocal, 2^56 over the Q26 loop gain plus one, gives Q30.
			c.divide_by_total_moveability_and_processed_resonance =
			    Q<30>::from_raw(static_cast<q31>(72057594037927936.0 / static_cast<double>(one_plus_loop_gain.raw())));
		}

		q31 gain = s.gain;
		if (s.mode != LowPassLadderMode::TRANSISTOR_24DB_DRIVE) {
			// Extra feedback, but only while the cutoff is low enough not to alias: × 0.536 rather than a half.
			if (curved.tanned <= Q<28>::from_raw(304587486)) {
				c.processed_resonance = mul_r_as_wrap<Q<30>>(c.processed_resonance, Q<31>::from_raw(1150000000));
			}
			else {
				c.processed_resonance = scale_shr_floor<1>(c.processed_resonance);
			}

			// 1 − (1 − r)², Q29, then the gain at 1/2 plus it.
			const Q<29> cut = sub_wrap(Q<29>::one(), Q<29>::from_raw(std::min(s.config.resonance, int32_t{536870911})));
			const Q<29> resonant = sub_wrap(Q<29>::one(), mul_as_wrap<Q<29>>(cut, cut));
			const Q<29> gain_modifier = add_wrap(Q<29>::from_raw(268435456), resonant);
			gain = mul_as_wrap<Q<31>>(Q<31>::from_raw(gain), gain_modifier).raw();
		}
		else {
			// The drive ladder's output is louder.
			gain = static_cast<q31>(gain * 0.8);
		}
		c.gain_compensation = gain;

		// An oversampling drive ladder's resonance is past its threshold, so it is never unresonant (see
		// kLowestOversamplingResonance).
		const bool unresonant = c.morph <= Q<31>{} && c.processed_resonance == Q<30>{}
		                        && c.divide_by_total_moveability_and_processed_resonance == Q<30>::one();
		c.mode = mode_of(ladder, unresonant ? Input::UNRESONANT : Input::FED_BACK);
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
	/// @param noise The block's noise stream, drawn once (twice when oversampled).
	/// @return The ladder's output, Q31.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static q31 step(const Coefficients& c, const EmptyBlock& /*b*/, State& s,
	                                                     q31 x, Noise& noise) {
		constexpr Ladder kLadder = ladder_of(M);
		constexpr Input kInput = input_of(M);
		if constexpr (kLadder == Ladder::T12) {
			return half_ladder<kInput>(c, s, x, noise);
		}
		else if constexpr (kLadder == Ladder::T24) {
			return full_ladder<kInput>(c, s, x, noise);
		}
		else {
			if constexpr (kLadder == Ladder::DRIVE_OVERSAMPLED) {
				// The input held for both halves of the sample works better than interpolating it; the first output
				// is dropped (the ladder itself filters what that crude downsampling would alias).
				std::ignore = drive_ladder<kInput>(c, s, x, noise);
			}
			return tanh<kOutputDrive>(drive_ladder<kInput>(c, s, x, noise));
		}
	}

private:
	/// @brief The resonance above which the input is saturated even with the morph at zero, Q30.
	static constexpr auto kSaturateAbove = Q<30>::from_raw(510000000);
	/// @brief The lowest resonance the drive ladder oversamples above, Q30: its threshold table's least entry, which
	/// the table holds at 2^-16 of the threshold, and any blend of two entries is at least that.
	static constexpr auto kLowestOversamplingResonance = Q<30>::from_raw(
	    q31{*std::ranges::min_element(low_pass_ladder_detail::kResonanceThresholdsForOversampling)} << 16);
	static_assert(kLowestOversamplingResonance > Q<30>{},
	              "an oversampling drive ladder must always feed back: DRIVE_OVERSAMPLED has no other mode");
	/// @brief The input saturation's drive, as a power of two.
	static constexpr uint32_t kInputDrive = 2;
	/// @brief The drive ladder's feedback saturation drive, as a power of two.
	static constexpr uint32_t kFeedbackDrive = 7;
	/// @brief The drive ladder's output saturation drive, as a power of two.
	static constexpr uint32_t kOutputDrive = 4;

	/// @brief This sample's moveability, Q31: the block's nudged by the channel's follower after one noise draw.
	[[nodiscard]] [[gnu::always_inline]] static Q<31> noisy_moveability(const Coefficients& c, State& s, Noise& noise) {
		const auto drawn = scale_shr_floor<2>(Q<31>::from_raw(static_cast<q31>(noise.next())));
		const auto follower = Q<31>::from_raw(Follower::template step<Follower::Mode::TRACK>(
		    Follower::Coefficients{}, EmptyBlock{}, s.noise, drawn.raw()));
		// m × n at Q30, read as Q31: m × (1 + n/2).
		return add_wrap(c.moveability, reinterpret_bits<Q<31>>(mul(c.moveability, follower)));
	}

	/// @brief The input less the scaled feedback, normalised, Q31; saturated with the morph's share of the input
	/// added when the morph is up or the resonance high.
	/// @param feedbacks_sum The sections' feedback, Q31.
	[[nodiscard]] [[gnu::always_inline]] static q31 scale_input(const Coefficients& c, q31 x, Q<31> feedbacks_sum) {
		const auto input = Q<31>::from_raw(x);
		// 2 × R × the feedback, Q31.
		const Q<31> fed_back = scale_shl_wrap<1>(mul_r_as_wrap<Q<31>>(feedbacks_sum, c.processed_resonance));
		const Q<31> scaled =
		    mul_r_as_wrap<Q<31>>(sub_wrap(input, fed_back), c.divide_by_total_moveability_and_processed_resonance);
		if (c.morph > Q<31>{} || c.processed_resonance > kSaturateAbove) {
			return tanh<kInputDrive>(add_wrap(scaled, mul_as_wrap<Q<31>>(input, c.morph))).raw();
		}
		return scaled.raw();
	}

	/// @brief The input normalised with nothing fed back, Q31: scale_input with the resonance and morph at zero.
	/// @details R = 0 and D = 1 (Q30), so nothing is fed back, and x × 2^30 rounded off its low 32 bits is (x + 2) >>
	/// 2, which the << 2 makes (x + 2) with its low two bits cleared, modulo 2^32.
	[[nodiscard]] [[gnu::always_inline]] static q31 unresonant_input(q31 x) {
		return static_cast<q31>((static_cast<uint32_t>(x) + 2u) & ~3u);
	}

	/// @brief One step of @p section at moveability @p m in mode @p M.
	template <OnePole::Mode M>
	[[nodiscard]] [[gnu::always_inline]] static q31 section_step(OnePole::State& section, Q<31> m, q31 x) {
		return OnePole::step<M>(OnePole::Coefficients{.m = m.raw(), .mode = M}, EmptyBlock{}, section, x);
	}

	/// @brief The last section's feedback amount: 1 / (1 + tan(f)), Q31, read as Q30.
	/// @details It "should" be halved onto Q30, but the ladder sounds better (and reaches full resonance) at twice it.
	[[nodiscard]] [[gnu::always_inline]] static Q<30> last_feedback(const Coefficients& c) {
		return reinterpret_bits<Q<30>>(c.divide_by_one_plus_tanned);
	}

	/// @brief The four sections' feedback summed at Q29, then reformatted to Q31.
	[[nodiscard]] [[gnu::always_inline]] static Q<31> full_feedback(const Coefficients& c, const State& s) {
		const Q<29> sum = add_wrap(add_wrap(OnePole::feedback_unshifted(s.sections[0], c.feedbacks[0]),
		                                    OnePole::feedback_unshifted(s.sections[1], c.feedbacks[1])),
		                           add_wrap(OnePole::feedback_unshifted(s.sections[2], c.feedbacks[2]),
		                                    OnePole::feedback_unshifted(s.sections[3], last_feedback(c))));
		return as_wrap<Q<31>>(sum);
	}

	/// @brief A ladder's output, Q31: its last section's, doubled (wrapping) to make up the level.
	[[nodiscard]] [[gnu::always_inline]] static q31 made_up(q31 y) {
		return scale_shl_wrap<1>(Q<31>::from_raw(y)).raw();
	}

	/// @brief One sample (Q31) through the half ladder, its input formed as @p I.
	template <Input I>
	[[nodiscard]] [[gnu::always_inline]] static q31 half_ladder(const Coefficients& c, State& s, q31 input,
	                                                            Noise& noise) {
		const Q<31> m = noisy_moveability(c, s, noise);
		q31 x = 0;
		if constexpr (I == Input::UNRESONANT) {
			x = unresonant_input(input);
		}
		else {
			const Q<31> feedbacks_sum = add_wrap(add_wrap(OnePole::feedback(s.sections[0], c.feedbacks[0]),
			                                              OnePole::feedback(s.sections[1], c.feedbacks[1])),
			                                     OnePole::feedback(s.sections[2], last_feedback(c)));
			x = scale_input(c, input, feedbacks_sum);
		}
		const q31 a = section_step<OnePole::Mode::LOW_PASS>(s.sections[0], m, x);
		const q31 b = section_step<OnePole::Mode::LOW_PASS>(s.sections[1], m, a);
		return made_up(section_step<OnePole::Mode::ALL_PASS>(s.sections[2], m, b));
	}

	/// @brief One sample (Q31) through the full ladder, its input formed as @p I.
	template <Input I>
	[[nodiscard]] [[gnu::always_inline]] static q31 full_ladder(const Coefficients& c, State& s, q31 input,
	                                                            Noise& noise) {
		const Q<31> m = noisy_moveability(c, s, noise);
		const q31 x = I == Input::UNRESONANT ? unresonant_input(input) : scale_input(c, input, full_feedback(c, s));
		return made_up(four_sections(s, m, x));
	}

	/// @brief One step (Q31) of the drive ladder, its feedback saturated (the input is not, as that is where it would
	/// alias most); the output is not yet saturated. The input is formed as @p I.
	template <Input I>
	[[nodiscard]] [[gnu::always_inline]] static q31 drive_ladder(const Coefficients& c, State& s, q31 input,
	                                                             Noise& noise) {
		const Q<31> m = noisy_moveability(c, s, noise);
		const q31 x = I == Input::UNRESONANT ? unresonant_input(input)
		                                     : scale_input(c, input, tanh<kFeedbackDrive>(full_feedback(c, s)));
		return made_up(four_sections(s, m, x));
	}

	/// @brief @p x through the four sections' low-passes in turn at moveability @p m.
	[[nodiscard]] [[gnu::always_inline]] static q31 four_sections(State& s, Q<31> m, q31 x) {
		const q31 a = section_step<OnePole::Mode::LOW_PASS>(s.sections[0], m, x);
		const q31 b = section_step<OnePole::Mode::LOW_PASS>(s.sections[1], m, a);
		const q31 d = section_step<OnePole::Mode::LOW_PASS>(s.sections[2], m, b);
		return section_step<OnePole::Mode::LOW_PASS>(s.sections[3], m, d);
	}
};

} // namespace deluge::dsp::fixed
