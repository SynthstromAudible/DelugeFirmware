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
#include "dsp-next/core/mode.h"
#include "dsp-next/core/q31_ops.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/delay/analog_impulse_response.h"
#include "dsp-next/fixed/shaping/tanh.h"

#include <cstdint>
#include <type_traits>

/// @file
/// @brief FeedbackShaper: the delay's feedback gain and saturation, analog or digital, as a processor kernel.
namespace deluge::dsp::fixed {

/// @brief The delay's feedback, scaled by the feedback amount and saturated: through the analog impulse response and
/// a tanh, or clipped digitally. Either way the result is shifted back up by 2.
/// @details
/// - **ANALOG**: y = tanh(r(x) × f, d) << 2, where r is the @ref AnalogImpulseResponse, f the feedback amount, d the
///   drive. Less headroom than digital: the analog saturation sounds fine driven.
/// - **DIGITAL**: y = sat29(x × f) << 2: the product clipped to 29 bits, ±2^28. More headroom than analog: a pure
///   digital clip sounds bad. The response's history is left untouched.
///
/// Each product is the high word of the 32x32 multiply, so a Q31 sample times a Q31 amount comes out Q30, and the
/// final shift by 2 brings the saturated value back up.
///
/// **Mode changes.** The response is nested in the State rather than chained, so a switch between blocks keeps its
/// history: a digital block leaves it as the last analog block did, and the next analog block resumes from it.
///
/// **The shift.** Both shifts act on values the saturation has already bounded, so neither leaves q31. The tanh's
/// table entries lie within ±32767, so its interpolated value lies within ±2^31 before the shift down by d + 2, and
/// the result within ±2^(29 − d); shifted up by 2, within ±2^(31 − d): ±2^23 at drive 8 and ±2^26 at drive 5. The
/// digital clip is within ±2^28, so shifted up by 2 within ±2^30.
struct FeedbackShaper {
	using value_type = q31; ///< A sample, Q31.
	using step_type = q31;  ///< A sample, Q31: one step per sample.

	/// @brief How the feedback is saturated.
	enum class Mode : uint8_t {
		ANALOG,  ///< Through the impulse response, then a tanh at the drive.
		DIGITAL, ///< Clipped to 29 bits; the impulse response holds still.
	};
	/// @brief DIGITAL first: it is the default, as `Settings::analog` defaults to false.
	using Modes = ModeList<Mode::DIGITAL, Mode::ANALOG>;

	/// @brief The bits the digital clip keeps: 3 fewer than a word, so the shift by 2 leaves a bit of headroom.
	static constexpr int32_t kDigitalBits = 32 - 3;
	/// @brief The shift that brings the saturated value back up.
	static constexpr int kOutputShift = 2;

	/// @brief What the owner sets.
	struct Settings {
		q31 feedback = 0;    ///< The feedback amount, Q31.
		bool analog = false; ///< Through the impulse response and a tanh; else clipped digitally.
		uint32_t drive = 8;  ///< The tanh drive, as a power of two: 8 on a sound, 5 on a host chain.
	};

	/// @brief What a step reads: the amount, the drive, and the mode the wrapper dispatches on once per block.
	struct Coefficients {
		q31 feedback = 0;          ///< The feedback amount, Q31.
		uint32_t drive = 8;        ///< The tanh drive, as a power of two.
		Mode mode = Mode::DIGITAL; ///< How the feedback is saturated.
	};

	/// @brief The impulse response's history. State{} is silence.
	struct State {
		AnalogImpulseResponse::State response{}; ///< The response's last 25 inputs, Q31; held still in DIGITAL.
	};

	/// @brief The coefficients for @p s: the amount and the drive unchanged, and `analog` as the mode.
	/// @param s What the owner sets.
	/// @return The amount, drive and mode a step reads.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) {
		return {.feedback = s.feedback, .drive = s.drive, .mode = s.analog ? Mode::ANALOG : Mode::DIGITAL};
	}

	/// @brief The analog shaping of the impulse response's output @p response: tanh(response × f, d) << 2, Q31.
	/// @details What ANALOG's step returns after stepping the response; for an owner stepping two channels' responses
	/// together (@ref AnalogImpulseResponse::step_pair).
	[[nodiscard]] [[gnu::always_inline]] static q31 analog(const Coefficients& c, q31 response) {
		return tanh(multiply_32x32_rshift32(response, c.feedback), c.drive) << kOutputShift;
	}

	/// @brief One sample of feedback, shaped.
	/// @tparam M The block's mode.
	/// @param c The amount and the drive.
	/// @param s The response's history: stepped in ANALOG, untouched in DIGITAL.
	/// @param x The delayed sample, Q31.
	/// @return tanh(r(x) × f, d) << 2 (ANALOG) or sat29(x × f) << 2 (DIGITAL), Q31.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static q31 step(const Coefficients& c, const EmptyBlock& /*b*/, State& s,
	                                                     q31 x) {
		if constexpr (M == Mode::ANALOG) {
			return analog(c, AnalogImpulseResponse::step({}, {}, s.response, x));
		}
		else {
			return signed_saturate<kDigitalBits>(multiply_32x32_rshift32(x, c.feedback)) << kOutputShift;
		}
	}
};

static_assert(processor_kernel<FeedbackShaper>);
static_assert(!std::is_polymorphic_v<FeedbackShaper>);

} // namespace deluge::dsp::fixed
