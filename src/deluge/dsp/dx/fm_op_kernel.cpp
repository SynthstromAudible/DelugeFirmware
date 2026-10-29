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

#include "fm_op_kernel.h"
#include "fm_core.h"

#ifdef HAVE_NEON

extern "C" void neon_fm_kernel(const int32_t* in, const int32_t* busin, int32_t* out, int count, int32_t phase0,
                               int32_t freq, int32_t gain1, int32_t dgain);

const int32_t __attribute__((aligned(16))) zeros[DX_MAX_N] = {0};

void FmOpKernel::compute_neon(int32_t* output, int n, const int32_t* input, int32_t phase0, int32_t freq, int32_t gain1,
                              int32_t dgain, bool add) {
	neon_fm_kernel(input != nullptr ? input : zeros, add ? output : zeros, output, n, phase0, freq, gain1, dgain);
}

#else

void FmOpKernel::compute_neon(int32_t* /*output*/, int /*n*/, const int32_t* /*input*/, int32_t /*phase0*/,
                              int32_t /*freq*/, int32_t /*gain1*/, int32_t /*dgain*/, bool /*add*/) {
}

#endif
