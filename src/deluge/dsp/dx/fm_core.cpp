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

#include "fm_core.h"
#include "fm_op_kernel.h"
#include "math_lut.h"

const FmAlgorithm FmCore::algorithms[32] = {
    {{0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}}, // 1
    {{0x01, 0x11, 0x11, 0x14, 0xc1, 0x14}}, // 2
    {{0xc1, 0x11, 0x14, 0x01, 0x11, 0x14}}, // 3
    {{0xc1, 0x11, 0x94, 0x01, 0x11, 0x14}}, // 4
    {{0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}}, // 5
    {{0xc1, 0x94, 0x01, 0x14, 0x01, 0x14}}, // 6
    {{0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}}, // 7
    {{0x01, 0x11, 0xc5, 0x14, 0x01, 0x14}}, // 8
    {{0x01, 0x11, 0x05, 0x14, 0xc1, 0x14}}, // 9
    {{0x01, 0x05, 0x14, 0xc1, 0x11, 0x14}}, // 10
    {{0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}}, // 11
    {{0x01, 0x05, 0x05, 0x14, 0xc1, 0x14}}, // 12
    {{0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}}, // 13
    {{0xc1, 0x05, 0x11, 0x14, 0x01, 0x14}}, // 14
    {{0x01, 0x05, 0x11, 0x14, 0xc1, 0x14}}, // 15
    {{0xc1, 0x11, 0x02, 0x25, 0x05, 0x14}}, // 16
    {{0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}}, // 17
    {{0x01, 0x11, 0x11, 0xc5, 0x05, 0x14}}, // 18
    {{0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}}, // 19
    {{0x01, 0x05, 0x14, 0xc1, 0x14, 0x14}}, // 20
    {{0x01, 0x14, 0x14, 0xc1, 0x14, 0x14}}, // 21
    {{0xc1, 0x14, 0x14, 0x14, 0x01, 0x14}}, // 22
    {{0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}}, // 23
    {{0xc1, 0x14, 0x14, 0x14, 0x04, 0x04}}, // 24
    {{0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}}, // 25
    {{0xc1, 0x05, 0x14, 0x01, 0x14, 0x04}}, // 26
    {{0x01, 0x05, 0x14, 0xc1, 0x14, 0x04}}, // 27
    {{0x04, 0xc1, 0x11, 0x14, 0x01, 0x14}}, // 28
    {{0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}}, // 29
    {{0x04, 0xc1, 0x11, 0x14, 0x04, 0x04}}, // 30
    {{0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}}, // 31
    {{0xc4, 0x04, 0x04, 0x04, 0x04, 0x04}}, // 32
};

int n_out(const FmAlgorithm& alg) {
	int count = 0;
	for (int i = 0; i < 6; i++) {
		if ((alg.ops[i] & 7) == OUT_BUS_ADD)
			count++;
	}
	return count;
}

void FmCore::render(int32_t* output, int n, FmOpParams* params, int algorithm, int32_t* fb_buf,
                    int32_t feedback_shift) {
	const FmAlgorithm alg = algorithms[algorithm];

	int simd_n = n;
	if (neon) {
		// tricky: (n%12) == 8 is not allowed in neon_fm_kernel().
		// otherwise, round up to the nearest multiple of 4
		int nmod = 1 + (n + 11) % 12;
		simd_n = nmod == 8 ? n + 4 : (n + 3) & ~3;
	}

	const int inv_n = (1 << 30) / n;
	bool has_contents[3] = {true, false, false};
	for (int op = 0; op < 6; op++) {
		int flags = alg.ops[op];
		bool add = (flags & OUT_BUS_ADD) != 0;
		FmOpParams& param = params[op];
		int inbus = (flags >> 4) & 3;
		int outbus = flags & 3;
		int32_t* outptr = (outbus == 0) ? output : buf_[outbus - 1].get();
		int32_t gain1 = param.gain_out;
		int32_t gain2 = Exp2::lookup(param.level_in - (14 * (1 << 24)));
		param.gain_out = gain2;
		int32_t dgain = div_n(gain2 - gain1 + (n >> 1), inv_n);

		if (gain1 >= kGainLevelThresh || gain2 >= kGainLevelThresh) {
			if (!has_contents[outbus]) {
				add = false;
			}
			if (inbus == 0 || !has_contents[inbus]) {
				// todo: more than one op in a feedback loop
				if ((flags & 0xc0) == 0xc0 && feedback_shift < 16) {
					// cout << op << " fb " << inbus << outbus << add << endl;
					FmOpKernel::compute_fb(outptr, n, param.phase, param.freq, gain1, gain2, dgain, fb_buf,
					                       feedback_shift, add);
				}
				else {
					// cout << op << " pure " << inbus << outbus << add << endl;
					FmOpKernel::compute_pure(outptr, simd_n, param.phase, param.freq, gain1, gain2, dgain, add, neon);
				}
			}
			else {
				// cout << op << " normal " << inbus << outbus << " " << param.freq << add << endl;
				FmOpKernel::compute(outptr, simd_n, buf_[inbus - 1].get(), param.phase, param.freq, gain1, gain2, dgain,
				                    add, neon);
			}
			has_contents[outbus] = true;
		}
		else if (!add) {
			has_contents[outbus] = false;
		}
		param.phase += param.freq * n;
	}
}
