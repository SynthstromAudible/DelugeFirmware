/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include "dsp-next/fixed/fft/real_dft.h"
#include "dsp-next/core/q31_ops.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/tables/sine_small.h"

#include <cassert>

namespace deluge::dsp::fixed::fft {

namespace {

constexpr int32_t kAngleBits = 16;    // a whole turn is 2^16
constexpr int32_t kTableSizeBits = 8; // tables::kSineSmall's 256 steps per turn
constexpr int32_t kQuarterTurn = 1 << (kTableSizeBits - 2);
constexpr int32_t kFractionShift = 16 + kTableSizeBits - kAngleBits; // brings the angle's sub-step bits to the top 16

} // namespace

// Accurate for N up to 65536, and probably a little past it.
void real_dft(std::span<const int32_t> in, std::span<Complex> out) {
	const auto n = static_cast<uint32_t>(in.size());
	assert(out.size() >= (n >> 1) + 1);
	for (uint32_t i = 0; i <= (n >> 1); i++) {
		int32_t sum_real = 0;
		int32_t sum_imaginary = 0;
		const uint32_t angle_increment = -((i << kAngleBits) / n);
		uint32_t angle = 0;

		for (const int32_t sample : in) {
			int32_t sine_index = static_cast<int32_t>(angle >> (kAngleBits - kTableSizeBits));
			sine_index &= (1 << kTableSizeBits) - 1;
			const int32_t sine1 = deluge::dsp::fixed::tables::kSineSmall[sine_index];
			const int32_t sine2 = deluge::dsp::fixed::tables::kSineSmall[sine_index + 1];

			int32_t cosine_index = sine_index + kQuarterTurn;
			cosine_index &= (1 << kTableSizeBits) - 1;
			const int32_t cosine1 = deluge::dsp::fixed::tables::kSineSmall[cosine_index];
			const int32_t cosine2 = deluge::dsp::fixed::tables::kSineSmall[cosine_index + 1];

			const uint32_t shifted = angle << kFractionShift;
			const int32_t fraction = static_cast<int32_t>(shifted & 65535);

			const int32_t sine = wrapping_add(sine1 << 16, (sine2 - sine1) * fraction);
			const int32_t cosine = wrapping_add(cosine1 << 16, (cosine2 - cosine1) * fraction);

			// Each product loses one bit of magnitude, made up for below.
			sum_real = multiply_accumulate_32x32_rshift32_rounded(sum_real, sample, cosine);
			sum_imaginary = multiply_accumulate_32x32_rshift32_rounded(sum_imaginary, sample, sine);

			angle += angle_increment;
		}

		out[i].r = sum_real << 1;
		out[i].i = sum_imaginary << 1;
	}
}

} // namespace deluge::dsp::fixed::fft
