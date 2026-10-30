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

#include "dsp-next/fixed/fft/complex.h"
#include "dsp-next/fixed/fft/twiddles.h"

#include <cstdint>
#include <span>

namespace deluge::dsp::fixed::fft {

/// @brief An unscaled int32 real FFT over lent tables and scratch: forward from 2^m real samples to 2^(m-1)+1 bins,
///        inverse back, each output bit for bit as tests/dsp/real_fft_records.h records it. A block-staged component:
///        no state between calls.
class RealFft {
public:
	/// @param twiddles Built tables (factor, stage and super twiddles all generated).
	/// @param scratch  2^magnitude entries the transforms work in.
	RealFft(const Twiddles& twiddles, std::span<Complex> scratch);

	[[nodiscard]] int32_t magnitude() const { return twiddles_.magnitude; }

	/// @brief @p in: 2^m samples; @p out: 2^(m-1)+1 bins. m ≥ kMinForwardMagnitude.
	void forward(std::span<const int32_t> in, std::span<Complex> out) const;

	/// @brief @p in: 2^(m-1)+1 bins; @p out: 2^m samples, which the transform also works in. m ≥ kMinMagnitude.
	void inverse(std::span<const Complex> in, std::span<int32_t> out) const;

private:
	Twiddles twiddles_;
	std::span<Complex> scratch_;
};

} // namespace deluge::dsp::fixed::fft
