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

#include "aligned_buf.h"
#include "dsp-next/fixed/dx7/algorithms.h"
#include "fm_op_kernel.h"

// TRICKY: neon_fm_kernel claims (n%12)==8 not allowed.
// make it 132 to allow 128 output with 4 byte padding
const static int DX_MAX_N = deluge::dsp::fixed::dx7::kMaxBlock;

class FmCore {
public:
	virtual ~FmCore() {};
	virtual void render(int32_t* output, int n, FmOpParams* params, int algorithm, int32_t* fb_buf,
	                    int32_t feedback_gain);
	bool neon = false;

protected:
	AlignedBuf<int32_t, DX_MAX_N> buf_[2]{};
};
