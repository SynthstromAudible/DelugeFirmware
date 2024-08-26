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
#include "fm_op_kernel.h"

#define div_n(base, inv_n) ((int)((((int64_t)(base)) * (int64_t)(inv_n)) >> 30))

// TRICKY: neon_fm_kernel claims (n%12)==8 not allowed.
// make it 132 to allow 128 output with 4 byte padding
const static int DX_MAX_N = 132;

class FmOperatorInfo {
public:
	int in;
	int out;
};

// operator should be considered inaudible when gain_out is below this
const int kGainLevelThresh = 1120;

enum FmOperatorFlags {
	OUT_BUS_ONE = 1 << 0,
	OUT_BUS_TWO = 1 << 1,
	OUT_BUS_ADD = 1 << 2,
	IN_BUS_ONE = 1 << 4,
	IN_BUS_TWO = 1 << 5,
	FB_IN = 1 << 6,
	FB_OUT = 1 << 7
};

class FmAlgorithm {
public:
	int ops[6];
};

class FmCore {
public:
	virtual ~FmCore() {};
	static void dump();
	virtual void render(int32_t* output, int n, FmOpParams* params, int algorithm, int32_t* fb_buf,
	                    int32_t feedback_gain);
	const static FmAlgorithm algorithms[32];
	bool neon = false;

protected:
	AlignedBuf<int32_t, DX_MAX_N> buf_[2];
};
