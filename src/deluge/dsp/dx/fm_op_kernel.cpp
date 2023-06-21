/*
 * Copyright 2012 Google Inc.
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

#include <cstdlib>

#define HAVE_NEON
#ifdef HAVE_NEON
// #include <cpu-features.h>
#endif

#include "fm_op_kernel.h"
#include "math_lut.h"

#ifdef HAVE_NEON

extern "C" void neon_fm_kernel(const int32_t* in, const int32_t* busin, int32_t* out, int count, int32_t phase0,
                               int32_t freq, int32_t gain1, int32_t dgain);

const int32_t __attribute__((aligned(16))) zeros[DX_MAX_N] = {0};

#endif

void FmOpKernel::compute(int32_t* output, int n, const int32_t* input, int32_t phase0, int32_t freq, int32_t gain1,
                         int32_t gain2, int32_t dgain, bool add, bool neon) {
	int32_t gain = gain1;
	int32_t phase = phase0;
	if (neon) {
#ifdef HAVE_NEON
		neon_fm_kernel(input, add ? output : zeros, output, n, phase0, freq, gain, dgain);
#endif
	}
	else {
		if (add) {
			for (int i = 0; i < n; i++) {
				gain += dgain;
				int32_t y = Sin::lookup(phase + input[i]);
				int32_t y1 = ((int64_t)y * (int64_t)gain) >> 24;
				output[i] += y1;
				phase += freq;
			}
		}
		else {
			for (int i = 0; i < n; i++) {
				gain += dgain;
				int32_t y = Sin::lookup(phase + input[i]);
				int32_t y1 = ((int64_t)y * (int64_t)gain) >> 24;
				output[i] = y1;
				phase += freq;
			}
		}
	}
}

void FmOpKernel::compute_pure(int32_t* output, int n, int32_t phase0, int32_t freq, int32_t gain1, int32_t gain2,
                              int32_t dgain, bool add, bool neon) {
	int32_t gain = gain1;
	int32_t phase = phase0;
	if (neon) {
#ifdef HAVE_NEON
		neon_fm_kernel(zeros, add ? output : zeros, output, n, phase0, freq, gain, dgain);
#endif
	}
	else {
		if (add) {
			for (int i = 0; i < n; i++) {
				gain += dgain;
				int32_t y = Sin::lookup(phase);
				int32_t y1 = ((int64_t)y * (int64_t)gain) >> 24;
				output[i] += y1;
				phase += freq;
			}
		}
		else {
			for (int i = 0; i < n; i++) {
				gain += dgain;
				int32_t y = Sin::lookup(phase);
				int32_t y1 = ((int64_t)y * (int64_t)gain) >> 24;
				output[i] = y1;
				phase += freq;
			}
		}
	}
}

#define noDOUBLE_ACCURACY
#define HIGH_ACCURACY

void FmOpKernel::compute_fb(int32_t* output, int n, int32_t phase0, int32_t freq, int32_t gain1, int32_t gain2,
                            int32_t dgain, int32_t* fb_buf, int fb_shift, bool add) {
	int32_t gain = gain1;
	int32_t phase = phase0;
	int32_t y0 = fb_buf[0];
	int32_t y = fb_buf[1];
	if (add) {
		for (int i = 0; i < n; i++) {
			gain += dgain;
			int32_t scaled_fb = (y0 + y) >> (fb_shift + 1);
			y0 = y;
			y = Sin::lookup(phase + scaled_fb);
			y = ((int64_t)y * (int64_t)gain) >> 24;
			output[i] += y;
			phase += freq;
		}
	}
	else {
		for (int i = 0; i < n; i++) {
			gain += dgain;
			int32_t scaled_fb = (y0 + y) >> (fb_shift + 1);
			y0 = y;
			y = Sin::lookup(phase + scaled_fb);
			y = ((int64_t)y * (int64_t)gain) >> 24;
			output[i] = y;
			phase += freq;
		}
	}
	fb_buf[0] = y0;
	fb_buf[1] = y;
}
