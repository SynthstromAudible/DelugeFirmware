/*
 * Copyright © 2015-2026 Synthstrom Audible Limited
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
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief AnalogImpulseResponse: the analog delay's 26-tap impulse response, as a scalar q31 processor kernel.
namespace deluge::dsp::fixed {

/// @brief The analog delay's 26-tap impulse response (Q31 taps), one channel's sample at a time.
/// @details A direct-form FIR over the last 25 inputs: the output for a sample is the input times the first tap plus
/// each of the last 25 inputs times its tap. Each product is the rounded high word of the 32x32 multiply, a Q31
/// input times a Q31 tap at Q30, and the sum is Q30: the output is its bits read as a Q31 sample, half the FIR's
/// value.
///
/// **Exactness.** The output is the sum of the 26 rounded products round(x[n − i] × tap_i >> 32). The sums wrap modulo
/// 2^32, and wrapping adds commute and associate, so the order the products are summed in does not change a bit.
/// `smmlar` adds a product's rounded high word into an accumulator: (acc << 32 + a × b + 2^31) >> 32 is
/// acc + round(a × b >> 32) modulo 2^32 exactly, because acc << 32 is a multiple of 2^32. The history carries the same
/// information as 25 pending partial sums would, so from State{} this form and a running-partial-sums form give the
/// same output for every input, silence after a long input included.
///
/// **Wrapping.** Each product is half the tap times the input, and the taps' magnitudes sum to about 2.27, so a sum
/// stays within about 1.13 of the input's peak: at full scale, an input whose signs follow the taps' takes a sum past
/// q31. The sums wrap modulo 2^32, as the target's `add` and `smmlar` compute them.
///
/// **Shape.** The tap loop stores nothing: the history is a ring of 25 inputs written twice, at `oldest` and
/// `oldest + 25`, so the 25 from `oldest` on are contiguous, oldest first, and a step writes two words after its sum.
/// A store in the tap loop puts a load→multiply→add→store chain behind each next load. Each tap is one `smmlar`
/// (`mla_r`) into one of two accumulators, the even taps' and the odd taps', so
/// neither waits on the other's multiply result. Each pair's history samples and taps are loaded an iteration
/// ahead of their multiplies, so the loads are not waited on. docs/dev/a9_dsp_performance.md has the cycle counts.
///
/// **Channels.** The response never mixes channels: `Stereo<AnalogImpulseResponse>` steps each channel through its own
/// State, exactly as a stereo response stepping both channels side by side.
struct AnalogImpulseResponse {
	using value_type = q31; ///< A sample, Q31.
	using step_type = q31;  ///< A sample, Q31: one step per sample.

	/// @brief The response's length, in taps.
	static constexpr std::size_t kTaps = 26;
	/// @brief The past inputs a step reads: one fewer than the taps, the first tap being the input's.
	static constexpr std::size_t kHistory = kTaps - 1;

	/// @brief The taps, Q31: a short pre-ring, the main lobe at taps 8 and 9, then a damped tail.
	static constexpr std::array<int32_t, kTaps> kResponse{
	    -3203916,   8857848,   24813136,  41537808, 35217472,  15195632,  -27538592, -61984128, 1944654848,
	    1813580928, 438462784, 101125088, 6042048,  -22429488, -46218864, -56638560, -64785312, -52108528,
	    -37256992,  -11863856, 1390352,   14663296, 12784464,  14254800,  5690912,   4490736,
	};

	/// @brief Taps 25 down to 1, Q31, in the history's order: history sample j, oldest first, is x[n − 25 + j] and
	/// meets tap `taps[j]`, tap 25 − j.
	struct HistoryTaps {
		std::array<int32_t, kHistory> taps; ///< Tap 25 − j at j, Q31.
	};

	/// @brief Taps 25 down to 1, in the history's order.
	static constexpr HistoryTaps kHistoryTaps = [] {
		HistoryTaps h{};
		for (std::size_t j = 0; j < kHistory; ++j) {
			h.taps[j] = kResponse[kHistory - j];
		}
		return h;
	}();

	/// @brief Nothing to set: the taps are fixed.
	struct Settings {};
	/// @brief Nothing to read: the taps are fixed.
	struct Coefficients {};

	/// @brief One channel's last 25 inputs. State{} is silence: every input 0.
	struct State {
		/// The last 25 inputs, Q31: entries i and i + 25 hold the same input, so the 25 from `oldest` on run oldest
		/// first.
		std::array<q31, 2 * kHistory> history{};
		/// Where the oldest input lies, in [0, 25).
		uint32_t oldest = 0;
	};

	/// @brief The coefficients: none.
	/// @return An empty Coefficients.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& /*s*/) {
		return {};
	}

	/// @brief One sample: the input times the first tap plus the last 25 inputs times theirs, the even taps and the
	/// odd taps summed apart and then together; then the input goes into the history over the oldest. The adds wrap.
	/// @param s The history, moved on by one sample.
	/// @param x The input, Q31.
	/// @return The response's output: the Q30 sum's bits, read as Q31.
	[[nodiscard]] [[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                                     q31 x) {
		const q31* const h = s.history.data() + s.oldest;
		const int32_t* const t = kHistoryTaps.taps.data();
		auto even = mul_r(Q<31>::from_raw(x), Q<31>::from_raw(kResponse[0]));
		auto odd = Q<30>::from_raw(0);
		q31 h0 = h[0];
		q31 h1 = h[1];
		int32_t t0 = t[0];
		int32_t t1 = t[1];
		for (std::size_t j = 2; j < kHistory - 1; j += 2) {
			const q31 next_h0 = h[j];
			const q31 next_h1 = h[j + 1];
			const int32_t next_t0 = t[j];
			const int32_t next_t1 = t[j + 1];
			even = mla_r(even, Q<31>::from_raw(h0), Q<31>::from_raw(t0));
			odd = mla_r(odd, Q<31>::from_raw(h1), Q<31>::from_raw(t1));
			h0 = next_h0;
			h1 = next_h1;
			t0 = next_t0;
			t1 = next_t1;
		}
		even = mla_r(even, Q<31>::from_raw(h0), Q<31>::from_raw(t0));
		odd = mla_r(odd, Q<31>::from_raw(h1), Q<31>::from_raw(t1));
		even = mla_r(even, Q<31>::from_raw(h[kHistory - 1]), Q<31>::from_raw(t[kHistory - 1]));

		push(s, x);
		return add_wrap(even, odd).raw();
	}

	/// @brief One sample of two channels at once, each through its own history: what @ref step gives each, the
	/// taps loaded once for both.
	/// @details The left and right sums are the two accumulators, so neither waits on the other's multiply result, as
	/// @ref step's even and odd sums do not. Exact by **Exactness**: each channel's products are summed in tap order
	/// rather than evens and odds apart.
	/// @param left The left channel's history, moved on by one sample.
	/// @param right The right channel's history, moved on by one sample.
	/// @param x_left The left input, Q31.
	/// @param x_right The right input, Q31.
	/// @return Each channel's response: the Q30 sums' bits, read as Q31.
	[[nodiscard]] [[gnu::always_inline]] static StereoSample step_pair(State& left, State& right, q31 x_left,
	                                                                   q31 x_right) {
		const q31* const h_left = left.history.data() + left.oldest;
		const q31* const h_right = right.history.data() + right.oldest;
		const int32_t* const t = kHistoryTaps.taps.data();
		auto sum_left = mul_r(Q<31>::from_raw(x_left), Q<31>::from_raw(kResponse[0]));
		auto sum_right = mul_r(Q<31>::from_raw(x_right), Q<31>::from_raw(kResponse[0]));
		// Unrolled by 5 (of 25): measured on the rig against the plain loop and a full unroll, which spills.
#pragma GCC unroll 5
		for (std::size_t j = 0; j < kHistory; ++j) {
			const auto tap = Q<31>::from_raw(t[j]);
			sum_left = mla_r(sum_left, Q<31>::from_raw(h_left[j]), tap);
			sum_right = mla_r(sum_right, Q<31>::from_raw(h_right[j]), tap);
		}
		push(left, x_left);
		push(right, x_right);
		return {.l = sum_left.raw(), .r = sum_right.raw()};
	}

private:
	/// @brief @p x into @p s's history over the oldest input, both copies.
	[[gnu::always_inline]] static void push(State& s, q31 x) {
		const uint32_t at = s.oldest;
		s.history[at] = x;
		s.history[at + kHistory] = x;
		s.oldest = at + 1 == kHistory ? 0 : at + 1;
	}
};

static_assert(processor_kernel<AnalogImpulseResponse>);
static_assert(!std::is_polymorphic_v<AnalogImpulseResponse>);

} // namespace deluge::dsp::fixed
