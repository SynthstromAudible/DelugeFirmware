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

#pragma once

#include <cstdint>

// The hand-written NEON FM kernel (neon_fm_kernel.s) is ARM-only; off-target the operator renders through
// dx7::Operator instead. __ARM_NEON is also defined when the *host* CPU is ARM (e.g. an Apple-Silicon sim build), so
// exclude DELUGE_HOST — the kernel only exists in firmware. DELUGE_DX_SCALAR forces the operator path too: the asm
// kernel computes sine by polynomial approximation while the operator uses the LUT, so they are NOT bit-identical,
// and the golden-master references must take the same path on every host.
#if defined(__ARM_NEON) && !defined(DELUGE_HOST) && !defined(DELUGE_DX_SCALAR)
#define HAVE_NEON
#endif

struct FmOpParams {
	int32_t level_in; // value to be computed (from level to gain[0])
	int32_t gain_out; // computed value (gain[1] to gain[0])
	int32_t freq;
	int32_t phase;
};

class FmOpKernel {
public:
#ifdef HAVE_NEON
	static constexpr bool kHaveNeon = true;
#else
	static constexpr bool kHaveNeon = false;
#endif

	// A modern operator through the NEON kernel, for n a multiple of 4 and not 12k + 8. The gain for sample i is
	// gain1 + (1 + i) * dgain. input is the modulation bus, or nullptr for a plain sine. Only called when kHaveNeon.
	static void compute_neon(int32_t* output, int n, const int32_t* input, int32_t phase0, int32_t freq, int32_t gain1,
	                         int32_t dgain, bool add);
};
