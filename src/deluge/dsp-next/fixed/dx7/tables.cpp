// Based on Dexed's math_lut (Copyright 2013 Google Inc., Apache License 2.0) and EngineMkI (Copyright (C) 2015-2017
// Pascal Gauthier, GPLv3).

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

#include "dsp-next/fixed/dx7/tables.h"

// Both spellings, as the engine's sources always included them: the unqualified libm calls below resolve to the same
// overloads (log2 of a float is the float overload), so the tables come out word for word.
#include <cmath>
#include <math.h> // NOLINT(*-deprecated-headers)

namespace deluge::dsp::fixed::dx7 {

namespace {

/// @brief The rounding term of the sine rotation's Q30 products.
constexpr int32_t kSinRound = 1 << 29;
/// @brief The octaves above the frequency table's base point; a lookup shifts down by 20 less its octave.
constexpr int kMaxLogFreqInt = 20;

void generate_exp2(int32_t* exp2tab) {
	double inc = exp2(1.0 / kExp2Samples);
	double y = 1 << 30;
	for (int i = 0; i < kExp2Samples; i++) {
		exp2tab[(i << 1) + 1] = (int32_t)floor(y + 0.5);
		y *= inc;
	}
	for (int i = 0; i < kExp2Samples - 1; i++) {
		exp2tab[i << 1] = exp2tab[(i << 1) + 3] - exp2tab[(i << 1) + 1];
	}
	exp2tab[(kExp2Samples << 1) - 2] = (1U << 31) - exp2tab[(kExp2Samples << 1) - 1];
}

/// @brief One sine cycle by rotating a Q30 vector through 1/1024 of a turn per point.
void generate_sine(int32_t* sintab) {
	double dphase = 2 * M_PI / kSinSamples;
	int32_t c = (int32_t)floor(cos(dphase) * (1 << 30) + 0.5);
	int32_t s = (int32_t)floor(sin(dphase) * (1 << 30) + 0.5);
	int32_t u = 1 << 30;
	int32_t v = 0;
	for (int i = 0; i < kSinSamples / 2; i++) {
		sintab[(i << 1) + 1] = (v + 32) >> 6;
		sintab[((i + kSinSamples / 2) << 1) + 1] = -((v + 32) >> 6);
		int32_t t = ((int64_t)u * (int64_t)s + (int64_t)v * (int64_t)c + kSinRound) >> 30;
		u = ((int64_t)u * (int64_t)c - (int64_t)v * (int64_t)s + kSinRound) >> 30;
		v = t;
	}
	for (int i = 0; i < kSinSamples - 1; i++) {
		sintab[i << 1] = sintab[(i << 1) + 3] - sintab[(i << 1) + 1];
	}
	sintab[(kSinSamples << 1) - 2] = -sintab[(kSinSamples << 1) - 1];
}

void generate_frequency(int32_t* freq_lut, double sample_rate) {
	double y = (1LL << (24 + kMaxLogFreqInt)) / sample_rate;
	double inc = pow(2, 1.0 / kFreqSamples);
	for (int i = 0; i < kFreqSamples + 1; i++) {
		freq_lut[i] = (int32_t)floor(y + 0.5);
		y *= inc;
	}
}

void generate_mki(uint16_t* sin_log, uint16_t* sin_exp) {
	float bitReso = kMkISamples;
	for (int i = 0; i < kMkISamples; i++) {
		float x1 = sin(((0.5 + i) / bitReso) * M_PI / 2.0);
		sin_log[i] = round(-1024 * log2(x1));
	}
	for (int i = 0; i < kMkISamples; i++) {
		float x1 = (pow(2, float(i) / bitReso) - 1) * 4096;
		sin_exp[i] = round(x1);
	}
}

} // namespace

void generate(Tables& tables) {
	generate_exp2(tables.exp2);
	generate_sine(tables.sine);
	generate_frequency(tables.frequency, kSampleRate);
	generate_mki(tables.mki_sin_log, tables.mki_sin_exp);
}

} // namespace deluge::dsp::fixed::dx7
