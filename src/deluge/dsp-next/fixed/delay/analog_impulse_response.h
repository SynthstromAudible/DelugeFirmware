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

#include "dsp-next/core/kernel.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "util/fixedpoint.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief AnalogImpulseResponse: the analog delay's 26-tap impulse response, as a scalar q31 processor kernel.
namespace deluge::dsp::fixed {

/// @brief The analog delay's 26-tap impulse response (Q31 taps), one channel's sample at a time.
/// @details A direct-form FIR over the last 25 inputs: the output for a sample is the input times the first tap plus
/// each of the last 25 inputs times its tap. Each product is the rounded high word of the 32x32 multiply, so a Q31
/// input comes out Q31 scaled by the tap.
///
/// **Exactness.** The output is the sum of the same 26 rounded products, round(x[n − i] × tap_i >> 32), as a form
/// that keeps running partial sums computes. The sums wrap modulo 2^32, and wrapping adds commute and associate, so
/// the order the products are summed in does not change a bit. `smmlar` adds a product's rounded high word into an
/// accumulator: (acc << 32 + a × b + 2^31) >> 32 is acc + round(a × b >> 32) modulo 2^32 exactly, because acc << 32 is
/// a multiple of 2^32. The history carries the same information as 25 pending sums would: from State{} either form
/// gives the same output for every input, silence after a long input included.
///
/// **Wrapping.** Each product is half the tap times the input, and the taps' magnitudes sum to about 2.27, so a sum
/// stays within about 1.13 of the input's peak: at full scale, an input whose signs follow the taps' takes a sum past
/// q31. The sums wrap modulo 2^32, as the target's `add` and `smmlar` compute them.
///
/// **Shape.** The tap loop stores nothing: the history is a ring of 25 inputs written twice, at `oldest` and
/// `oldest + 25`, so the 25 from `oldest` on are contiguous, oldest first, and a step writes two words after its sum.
/// A store in the tap loop puts a load→multiply→add→store chain behind each next load. Each tap is one `smmlar`
/// (multiply_accumulate_32x32_rshift32_rounded) into one of two accumulators, the even taps' and the odd taps', so
/// neither waits on the other's multiply result. Each pair's history samples and taps are loaded an iteration
/// ahead of their multiplies, so the loads are not waited on. docs/dev/a9_dsp_performance.md has the cycle counts.
///
/// **No calls.** The taps and the history are built-in arrays, read through plain pointers and subscripts: a
/// `std::array` member function is an out-of-line call per use under the app's `-fno-inline-functions`, so the step
/// makes no call under either the app's flags or the DSP library's, wherever it is instantiated.
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
	// A built-in array, not std::array: see **No calls** above.
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
	static constexpr int32_t kResponse[kTaps]{
	    -3203916,   8857848,   24813136,  41537808, 35217472,  15195632,  -27538592, -61984128, 1944654848,
	    1813580928, 438462784, 101125088, 6042048,  -22429488, -46218864, -56638560, -64785312, -52108528,
	    -37256992,  -11863856, 1390352,   14663296, 12784464,  14254800,  5690912,   4490736,
	};

	/// @brief Taps 25 down to 1, Q31, in the history's order: history sample j, oldest first, is x[n − 25 + j] and
	/// meets tap `taps[j]`, tap 25 − j.
	struct HistoryTaps {
		// A built-in array, not std::array: see **No calls** above.
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
		int32_t taps[kHistory]; ///< Tap 25 − j at j, Q31.
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
		/// first. A built-in array, not std::array: see **No calls** above.
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
		q31 history[2 * kHistory]{};
		/// Where the oldest input lies, in [0, 25).
		uint32_t oldest = 0;
	};

	/// @brief The coefficients: none.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& /*s*/) {
		return {};
	}

	/// @brief One sample: the input times the first tap plus the last 25 inputs times theirs, the even taps and the
	/// odd taps summed apart and then together; then the input goes into the history over the oldest. The adds wrap.
	/// @param s The history, moved on by one sample.
	/// @param x The input, Q31.
	/// @return The response's output, Q31.
	[[nodiscard]] [[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                                     q31 x) {
		const q31* const h = s.history + s.oldest;
		const int32_t* const t = kHistoryTaps.taps;
		q31 even = multiply_32x32_rshift32_rounded(x, kResponse[0]);
		q31 odd = 0;
		q31 h0 = h[0];
		q31 h1 = h[1];
		int32_t t0 = t[0];
		int32_t t1 = t[1];
		for (std::size_t j = 2; j < kHistory - 1; j += 2) {
			const q31 next_h0 = h[j];
			const q31 next_h1 = h[j + 1];
			const int32_t next_t0 = t[j];
			const int32_t next_t1 = t[j + 1];
			even = multiply_accumulate_32x32_rshift32_rounded(even, h0, t0);
			odd = multiply_accumulate_32x32_rshift32_rounded(odd, h1, t1);
			h0 = next_h0;
			h1 = next_h1;
			t0 = next_t0;
			t1 = next_t1;
		}
		even = multiply_accumulate_32x32_rshift32_rounded(even, h0, t0);
		odd = multiply_accumulate_32x32_rshift32_rounded(odd, h1, t1);
		even = multiply_accumulate_32x32_rshift32_rounded(even, h[kHistory - 1], t[kHistory - 1]);

		const uint32_t at = s.oldest;
		s.history[at] = x;
		s.history[at + kHistory] = x;
		s.oldest = at + 1 == kHistory ? 0 : at + 1;
		return wrapping_add(even, odd);
	}
};

static_assert(processor_kernel<AnalogImpulseResponse>);
static_assert(!std::is_polymorphic_v<AnalogImpulseResponse>);

} // namespace deluge::dsp::fixed
