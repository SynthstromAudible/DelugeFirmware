/*
 * Copyright 2013 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <math.h>

#include "engine.h"
#include "math_lut.h"

#ifdef _MSC_VER
#define exp2(arg) pow(2.0, arg)
#endif

static void exp2_init(int32_t* exp2tab) {
	double inc = exp2(1.0 / EXP2_N_SAMPLES);
	double y = 1 << 30;
	for (int i = 0; i < EXP2_N_SAMPLES; i++) {
		exp2tab[(i << 1) + 1] = (int32_t)floor(y + 0.5);
		y *= inc;
	}
	for (int i = 0; i < EXP2_N_SAMPLES - 1; i++) {
		exp2tab[i << 1] = exp2tab[(i << 1) + 3] - exp2tab[(i << 1) + 1];
	}
	exp2tab[(EXP2_N_SAMPLES << 1) - 2] = (1U << 31) - exp2tab[(EXP2_N_SAMPLES << 1) - 1];
}

static double dtanh(double y) {
	return 1 - y * y;
}

static void tanh_init(int32_t* tanhtab) {
	double step = 4.0 / TANH_N_SAMPLES;
	double y = 0;
	for (int i = 0; i < TANH_N_SAMPLES; i++) {
		tanhtab[(i << 1) + 1] = (1 << 24) * y + 0.5;
		// printf("%d\n", tanhtab[(i << 1) + 1]);
		//  Use a basic 4th order Runge-Kutte to compute tanh from its
		//  differential equation.
		double k1 = dtanh(y);
		double k2 = dtanh(y + 0.5 * step * k1);
		double k3 = dtanh(y + 0.5 * step * k2);
		double k4 = dtanh(y + step * k3);
		double dy = (step / 6) * (k1 + k4 + 2 * (k2 + k3));
		y += dy;
	}
	for (int i = 0; i < TANH_N_SAMPLES - 1; i++) {
		tanhtab[i << 1] = tanhtab[(i << 1) + 3] - tanhtab[(i << 1) + 1];
	}
	int32_t lasty = (1 << 24) * y + 0.5;
	tanhtab[(TANH_N_SAMPLES << 1) - 2] = lasty - tanhtab[(TANH_N_SAMPLES << 1) - 1];
}

#define R (1 << 29)
static void sin_init(int32_t* sintab) {
	double dphase = 2 * M_PI / SIN_N_SAMPLES;
	int32_t c = (int32_t)floor(cos(dphase) * (1 << 30) + 0.5);
	int32_t s = (int32_t)floor(sin(dphase) * (1 << 30) + 0.5);
	int32_t u = 1 << 30;
	int32_t v = 0;
	for (int i = 0; i < SIN_N_SAMPLES / 2; i++) {
#ifdef SIN_DELTA
		sintab[(i << 1) + 1] = (v + 32) >> 6;
		sintab[((i + SIN_N_SAMPLES / 2) << 1) + 1] = -((v + 32) >> 6);
#else
		sintab[i] = (v + 32) >> 6;
		sintab[i + SIN_N_SAMPLES / 2] = -((v + 32) >> 6);
#endif
		int32_t t = ((int64_t)u * (int64_t)s + (int64_t)v * (int64_t)c + R) >> 30;
		u = ((int64_t)u * (int64_t)c - (int64_t)v * (int64_t)s + R) >> 30;
		v = t;
	}
#ifdef SIN_DELTA
	for (int i = 0; i < SIN_N_SAMPLES - 1; i++) {
		sintab[i << 1] = sintab[(i << 1) + 3] - sintab[(i << 1) + 1];
	}
	sintab[(SIN_N_SAMPLES << 1) - 2] = -sintab[(SIN_N_SAMPLES << 1) - 1];
#else
	sintab[SIN_N_SAMPLES] = 0;
#endif
}

#define SAMPLE_SHIFT (24 - FREQ_LG_N_SAMPLES)
#define MAX_LOGFREQ_INT 20
static void freq_lut_init(int32_t* freq_lut, double sample_rate) {
	double y = (1LL << (24 + MAX_LOGFREQ_INT)) / sample_rate;
	double inc = pow(2, 1.0 / FREQ_N_SAMPLES);
	for (int i = 0; i < FREQ_N_SAMPLES + 1; i++) {
		freq_lut[i] = (int32_t)floor(y + 0.5);
		y *= inc;
	}
}

// Note: if logfreq is more than 20.0, the results will be inaccurate. However,
// that will be many times the Nyquist rate.
int32_t Freqlut::lookup(int32_t logfreq) {
	int ix = (logfreq & 0xffffff) >> SAMPLE_SHIFT;

	int32_t y0 = dxEngine->freq_lut[ix];
	int32_t y1 = dxEngine->freq_lut[ix + 1];
	int lowbits = logfreq & ((1 << SAMPLE_SHIFT) - 1);
	int32_t y = y0 + ((((int64_t)(y1 - y0) * (int64_t)lowbits)) >> SAMPLE_SHIFT);
	int hibits = logfreq >> 24;
	return y >> (MAX_LOGFREQ_INT - hibits);
}

void dx_init_lut_data() {
	exp2_init(dxEngine->exp2tab);
	tanh_init(dxEngine->tanhtab);
	sin_init(dxEngine->sintab);
	freq_lut_init(dxEngine->freq_lut, 44100);
}
