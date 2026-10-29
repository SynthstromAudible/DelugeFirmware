/*
 * Derived from Ne10's int32 real FFT (modules/dsp/NE10_fft.c and NE10_fft_int32.c), under its licences:
 *
 *   Copyright 2014-16 ARM Limited and Contributors.
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
 *
 * Ne10's FFT derives from Kiss FFT, under its licence:
 *
 *  Copyright (c) 2003-2010, Mark Borgerding
 *
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without modification, are permitted provided that
 *  the following conditions are met:
 *
 *      * Redistributions of source code must retain the above copyright notice, this list of conditions and the
 *        following disclaimer.
 *      * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
 *        the following disclaimer in the documentation and/or other materials provided with the distribution.
 *      * Neither the author nor the names of any contributors may be used to endorse or promote products derived
 *        from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *  PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 *  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *  DAMAGE.
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

#include "dsp-next/fixed/fft/twiddles.h"

#include <cassert>
#include <cmath>

namespace deluge::dsp::fixed::fft {

namespace {

// The phase is float arithmetic: π is rounded to float, and every int operand converts at its step, left to right.
// cos and sin then take the phase promoted to double, and the scaling and rounding are double. The records pin this
// order and these types, so the expressions below keep them.
constexpr float kPi = static_cast<float>(3.1415926535897932384626433832795);
constexpr int32_t kF2I32Max = 2147483647; // full-scale Q31

constexpr int32_t kAlgorithmDefault = 0; // the factor list's closing flag: a power-of-two decomposition

} // namespace

Layout lay_out(std::span<std::byte> storage, int32_t magnitude) {
	assert(magnitude >= kMinMagnitude && magnitude <= kMaxMagnitude);
	assert(storage.size() >= storage_bytes(magnitude));
	assert(reinterpret_cast<std::uintptr_t>(storage.data()) % 8 == 0);
	const std::size_t half = std::size_t{1} << (magnitude - 1);
	auto* factors = reinterpret_cast<int32_t*>(storage.data());
	auto* stage = reinterpret_cast<Complex*>(factors + (kMaxFactors * 2));
	Complex* super = stage + half;
	Complex* scratch = super + (half / 2);
	return {
	    .twiddles = {.magnitude = magnitude,
	                 .factors = {factors, static_cast<std::size_t>(kMaxFactors * 2)},
	                 .stage = {stage, half},
	                 .super = {super, half / 2}},
	    .scratch = {scratch, 2 * half},
	};
}

void factor(Twiddles& twiddles) {
	const int32_t length = twiddles.complex_length();
	assert(twiddles.magnitude >= kMinMagnitude && twiddles.magnitude <= kMaxMagnitude);
	std::span<int32_t> facbuf = twiddles.factors;
	int32_t n = length;
	int32_t p = 0;
	int32_t i = 1;
	int32_t stages = 0;
	do {
		if (n == 8) {
			p = 8;
		}
		else if ((n % 4) == 0) {
			p = 4;
		}
		else {
			p = 2;
		}
		n /= p;
		facbuf[2 * i] = p;
		facbuf[(2 * i) + 1] = n;
		i++;
		stages++;
	} while (n > 1);
	facbuf[0] = stages;
	facbuf[1] = length / p;
	facbuf[2 * i] = kAlgorithmDefault;
}

void generate_stage_twiddles(Twiddles& twiddles) {
	const int32_t ncfft = twiddles.complex_length();
	std::span<const int32_t> factors = twiddles.factors;
	Complex* out = twiddles.stage.data();
	int32_t fstride = factors[1];
	// The first stage needs none: each later stage s, from the last back to the second, appends its (radix − 1) ×
	// remaining-length table.
	for (int32_t s = factors[0] - 1; s > 0; s--) {
		const int32_t radix = factors[2 * s];
		fstride /= radix;
		const int32_t mstride = factors[(2 * s) + 1];
		for (int32_t j = 0; j < mstride; j++) {
			for (int32_t k = 1; k < radix; k++) {
				const float phase = -2 * kPi * fstride * k * j / ncfft;
				Complex& entry = out[(mstride * (k - 1)) + j];
				entry.r = static_cast<int32_t>(std::floor(0.5f + kF2I32Max * std::cos(static_cast<double>(phase))));
				entry.i = static_cast<int32_t>(std::floor(0.5f + kF2I32Max * std::sin(static_cast<double>(phase))));
			}
		}
		out += mstride * (radix - 1);
	}
}

void generate_super_twiddles(Twiddles& twiddles) {
	const int32_t ncfft = twiddles.complex_length();
	Complex* out = twiddles.super.data();
	for (int32_t j = 0; j < ncfft / 2; j++) {
		const float phase = -kPi * (static_cast<float>(j + 1) / ncfft + 0.5f);
		out->r = static_cast<int32_t>(std::floor(0.5f + kF2I32Max * std::cos(static_cast<double>(phase))));
		out->i = static_cast<int32_t>(std::floor(0.5f + kF2I32Max * std::sin(static_cast<double>(phase))));
		out++;
	}
}

} // namespace deluge::dsp::fixed::fft
