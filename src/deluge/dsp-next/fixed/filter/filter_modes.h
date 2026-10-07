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

#include "dsp-next/core/types.h"
#include "dsp-next/fixed/curves/instant_tan.h"
#include "util/fixedpoint.h"
#include "util/param_functions.h" // lshiftAndSaturate

#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief The ladder low-pass's modes.
enum class LowPassLadderMode : uint8_t {
	TRANSISTOR_12DB,       ///< The half ladder: two low-pass sections and an all-pass.
	TRANSISTOR_24DB,       ///< The full ladder: four low-pass sections.
	TRANSISTOR_24DB_DRIVE, ///< The full ladder with saturated feedback, oversampled at high resonance.
};

/// @brief The state-variable filter's modes as a low-pass slot uses them, and as a high-pass slot does.
enum class SvfMode : uint8_t {
	BAND,  ///< The morph sweeps low → band → high.
	NOTCH, ///< The morph sweeps low → high, through the notch their sum makes.
};

/// @brief One block's settings of a filter (each as the param's final value).
struct FilterConfig {
	/// The cutoff, Q31: lifted by 2^5 (saturating) onto @ref instant_tan's angle, so 2^26 and above reach the top of
	/// its table.
	q31 frequency;
	/// The resonance, Q29 at most (0 to 536870911: four times it is the full Q31 range).
	q31 resonance;
	/// The morph, Q29: each filter lifts it to Q31 (by 2^2, saturating) before use.
	q31 morph;
	/// The volume going in, Q31; configure returns it compensated.
	q31 gain;
	/// Whether the drive ladder may oversample: false under CPU load.
	bool allow_oversampling;
};

/// @brief A cutoff warped onto the filters' frequency scale, with the reciprocals the filters build on.
struct CurvedFrequency {
	/// tan(f), Q28 (1 is 268435456).
	q31 tanned;
	/// 1 / (1 + tan(f)), Q31: the reciprocal of 1 at Q27 plus half the Q28 tan(f).
	q31 divide_by_one_plus_tanned;
	/// The moveability tan(f) / (1 + tan(f)), Q31.
	q31 fc;
};

/// @brief Warps @p frequency (Q31, see @ref FilterConfig::frequency) through tan, and derives 1 / (1 + tan) and the
/// moveability from it.
[[nodiscard]] inline CurvedFrequency curve_frequency(q31 frequency) {
	/// One at Q27 (where half of a Q28 tan lands).
	constexpr int32_t kOneQ27 = 134217728;
	/// One at Q58: divided by a Q27 value, it gives the reciprocal at Q31.
	constexpr double kOneQ58 = 288230376151711744.0;
	const q31 tanned = instant_tan(lshiftAndSaturate<5>(frequency));
	const auto divide_by_one_plus_tanned = static_cast<q31>(kOneQ58 / static_cast<double>(kOneQ27 + (tanned >> 1)));
	return {
	    .tanned = tanned,
	    .divide_by_one_plus_tanned = divide_by_one_plus_tanned,
	    .fc = multiply_32x32_rshift32_rounded(tanned, divide_by_one_plus_tanned) << 4,
	};
}

} // namespace deluge::dsp::fixed
