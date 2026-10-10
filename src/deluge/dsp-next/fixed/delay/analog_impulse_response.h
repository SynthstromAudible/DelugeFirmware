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

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief AnalogImpulseResponse: the analog delay's 26-tap impulse response, as a scalar q31 processor kernel.
namespace deluge::dsp::fixed {

/// @brief The analog delay's 26-tap impulse response (Q31 taps), one channel's sample at a time.
/// @details A direct-form FIR kept as running partial sums: each input adds its product with every tap into the sums
/// still waiting to come out, so the output for a sample is the oldest sum plus the input times the first tap. Each
/// product is the rounded high word of the 32x32 multiply, so a Q31 input comes out Q31 scaled by the tap.
///
/// **Wrapping.** Each product is half the tap times the input, and the taps' magnitudes sum to about 2.27, so a sum
/// stays within about 1.13 of the input's peak: at full scale, an input whose signs follow the taps' takes a sum past
/// q31. The sums wrap modulo 2^32, as the target's `add` computes them.
///
/// **Channels.** The response never mixes channels: `Stereo<AnalogImpulseResponse>` steps each channel through its own
/// State, exactly as a stereo response stepping both channels side by side.
struct AnalogImpulseResponse {
	using value_type = q31; ///< A sample, Q31.
	using step_type = q31;  ///< A sample, Q31: one step per sample.

	/// @brief The response's length, in taps.
	static constexpr std::size_t kTaps = 26;
	/// @brief The sums still waiting to come out after a step: one fewer than the taps.
	static constexpr std::size_t kPending = kTaps - 1;

	/// @brief The taps, Q31: a short pre-ring, the main lobe at taps 8 and 9, then a damped tail.
	static constexpr std::array<int32_t, kTaps> kResponse{
	    -3203916,   8857848,   24813136,  41537808, 35217472,  15195632,  -27538592, -61984128, 1944654848,
	    1813580928, 438462784, 101125088, 6042048,  -22429488, -46218864, -56638560, -64785312, -52108528,
	    -37256992,  -11863856, 1390352,   14663296, 12784464,  14254800,  5690912,   4490736,
	};

	/// @brief Nothing to set: the taps are fixed.
	struct Settings {};
	/// @brief Nothing to read: the taps are fixed.
	struct Coefficients {};

	/// @brief One channel's sums. State{} is silence: every sum 0.
	struct State {
		/// The sums still to come out: entry i is the output i + 1 samples from now, less the inputs yet to arrive,
		/// Q31.
		std::array<q31, kPending> pending{};
	};

	/// @brief The coefficients: none.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& /*s*/) {
		return {};
	}

	/// @return @p sample (Q31) times tap @p index, the rounded high word: Q31 scaled by the tap.
	[[nodiscard]] [[gnu::always_inline]] static q31 tap(q31 sample, std::size_t index) {
		return multiply_32x32_rshift32_rounded(sample, kResponse[index]);
	}

	/// @brief One sample: the oldest sum plus the input times the first tap; then each sum moves one nearer, the
	/// input times its tap added, and the last takes the input times the last tap alone. The adds wrap.
	/// @param s The sums, moved on by one sample.
	/// @param x The input, Q31.
	/// @return The response's output, Q31.
	[[nodiscard]] [[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                                     q31 x) {
		const q31 output = wrapping_add(s.pending[0], tap(x, 0));
		for (std::size_t i = 1; i < kPending; i++) {
			s.pending[i - 1] = wrapping_add(s.pending[i], tap(x, i));
		}
		s.pending[kPending - 1] = tap(x, kPending);
		return output;
	}
};

static_assert(processor_kernel<AnalogImpulseResponse>);
static_assert(!std::is_polymorphic_v<AnalogImpulseResponse>);

} // namespace deluge::dsp::fixed
