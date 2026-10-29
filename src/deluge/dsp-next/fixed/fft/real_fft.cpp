/*
 * Derived from Ne10's int32 FFT kernels (modules/dsp/NE10_fft_int32.neonintrinsic.c), under its licence:
 *
 *   Copyright 2013-16 ARM Limited and Contributors.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of ARM Limited nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY ARM LIMITED AND CONTRIBUTORS "AS IS" AND
 *   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL ARM LIMITED AND CONTRIBUTORS BE LIABLE FOR ANY
 *   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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

#include "dsp-next/fixed/fft/real_fft.h"
#include "dsp-next/fixed/fft/radix.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>

namespace deluge::dsp::fixed::fft {

RealFft::RealFft(const Twiddles& twiddles, std::span<Complex> scratch) : twiddles_{twiddles}, scratch_{scratch} {
	assert(scratch.size() >= std::size_t{1} << twiddles.magnitude);
}

void RealFft::forward(std::span<const int32_t> in, std::span<Complex> out) const {
	const int32_t ncfft = twiddles_.complex_length();
	assert(twiddles_.magnitude >= kMinForwardMagnitude);
	assert(in.size() >= static_cast<std::size_t>(2 * ncfft));
	assert(out.size() >= static_cast<std::size_t>(ncfft + 1));
	const auto* samples = reinterpret_cast<const Complex*>(in.data());

	if (ncfft == 8) {
		// The radix-8 stage runs four butterflies a pass, and an 8-point transform is one of them: the pass reads
		// three values past the input and writes four groups of eight. It runs on locals, padded with zeros; the
		// first lane, outputs 0–7, is the transform.
		std::array<Complex, 11> padded{};
		std::copy_n(samples, 8, padded.begin());
		std::array<Complex, 32> wide;
		detail::radix8x4<detail::Direction::Forward>(wide.data(), padded.data(), 1);
		detail::split_forward(out.data(), wide.data(), twiddles_.super.data(), ncfft);
		return;
	}

	Complex* transformed = scratch_.data();
	Complex* buffer = scratch_.data() + ncfft;
	detail::mixed_radix<detail::Direction::Forward>(transformed, samples, twiddles_.factors, twiddles_.stage.data(),
	                                                buffer);
	detail::split_forward(out.data(), transformed, twiddles_.super.data(), ncfft);
}

void RealFft::inverse(std::span<const Complex> in, std::span<int32_t> out) const {
	const int32_t ncfft = twiddles_.complex_length();
	assert(twiddles_.magnitude >= kMinMagnitude);
	assert(in.size() >= static_cast<std::size_t>(ncfft + 1));
	assert(out.size() >= static_cast<std::size_t>(2 * ncfft));
	Complex* split = scratch_.data();
	Complex* buffer = scratch_.data() + ncfft;
	auto* samples = reinterpret_cast<Complex*>(out.data());

	detail::split_inverse(split, in.data(), twiddles_.super.data(), ncfft);
	switch (ncfft) {
	case 4:
		detail::fft4_inverse(samples, split);
		break;
	case 8:
		detail::fft8_inverse(samples, split);
		break;
	case 16:
		detail::fft16_inverse(samples, split, twiddles_.stage.data());
		break;
	default:
		detail::mixed_radix<detail::Direction::Inverse>(samples, split, twiddles_.factors, twiddles_.stage.data(),
		                                                buffer);
		break;
	}
}

} // namespace deluge::dsp::fixed::fft
