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

#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"

#include <cstdint>

/// @file
/// @brief The output stage's monitoring fold: the input frame by a fixed shift, to add to the output.
namespace deluge::dsp::fixed {

/// @brief How the output stage folds an input frame.
enum class OutputFold : uint8_t {
	STEREO,       ///< Left to left, right to right.
	LEFT_ONLY,    ///< Left to both: a mono mic or line.
	DIFFERENTIAL, ///< (l ≫ (shift + 1)) − (r ≫ shift) to both: a balanced line.
};

/// @brief The output stage's fold of @p in, each channel ≫ @p shift (the output's gain doublings), to add to its frame.
/// @details DIFFERENTIAL halves the left and not the right, the difference a balanced line's monitoring is recorded
/// at. It wraps as the target's `sub` does.
/// @param fold The fold.
/// @param in The input frame, Q31.
/// @param shift The output's gain doublings.
/// @return The frame to add to the output.
[[nodiscard]] [[gnu::always_inline]] inline StereoSample output_monitor_fold(OutputFold fold, StereoSample in,
                                                                             int shift) {
	switch (fold) {
	case OutputFold::DIFFERENTIAL: {
		const q31 v = wrapping_sub(in.l >> (shift + 1), in.r >> shift);
		return {.l = v, .r = v};
	}
	case OutputFold::LEFT_ONLY:
		return {.l = in.l >> shift, .r = in.l >> shift};
	case OutputFold::STEREO:
	default:
		return {.l = in.l >> shift, .r = in.r >> shift};
	}
}

} // namespace deluge::dsp::fixed
