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
#include "dsp-next/core/mono.h"
#include "dsp-next/fixed/dx7/operator.h"
#include "engine.h"
#include "fm_op_kernel.h"

namespace dx7 = deluge::dsp::fixed::dx7;

void FmCore::render(int32_t* output, int n, FmOpParams* params, int algorithm, int32_t* fb_buf,
                    int32_t feedback_shift) {
	const auto& alg = dx7::kAlgorithms[algorithm];

	int simd_n = n;
	if (neon) {
		// tricky: (n%12) == 8 is not allowed in neon_fm_kernel().
		// otherwise, round up to the nearest multiple of 4
		int nmod = 1 + (n + 11) % 12;
		simd_n = nmod == 8 ? n + 4 : (n + 3) & ~3;
	}

	const auto len = static_cast<std::size_t>(n);
	bool has_contents[3] = {true, false, false};
	for (int op = 0; op < 6; op++) {
		const int flags = alg[op];
		const bool add_flag = (flags & dx7::OUT_BUS_ADD) != 0;
		FmOpParams& param = params[op];
		const int inbus = (flags >> 4) & 3;
		const int outbus = flags & 3;
		int32_t* outptr = (outbus == 0) ? output : buf_[outbus - 1].get();
		const bool add = add_flag && has_contents[outbus];
		dx7::Input input = dx7::Input::BUS;
		if (inbus == 0 || !has_contents[inbus]) {
			// todo: more than one op in a feedback loop
			const bool feedback = (flags & 0xc0) == 0xc0 && feedback_shift < 16;
			input = feedback ? dx7::Input::FEEDBACK : dx7::Input::NONE;
		}

		deluge::dsp::Mono<dx7::Operator> unit{{.engine = dx7::Engine::MODERN,
		                                       .input = input,
		                                       .output = add ? dx7::Output::ADD : dx7::Output::SET,
		                                       .level_in = param.level_in,
		                                       .freq = param.freq,
		                                       .feedback_shift = feedback_shift,
		                                       .tables = &dxEngine->tables}};
		unit.on<&dx7::Operator::resume>(dx7::OperatorState{.phase = param.phase, .gain_out = param.gain_out},
		                                dx7::FeedbackHistory{.y0 = fb_buf[0], .y1 = fb_buf[1]});

		if (dx7::Operator::audible(unit.coefficients(), unit.state().op, len)) {
			if (neon && FmOpKernel::kHaveNeon && input != dx7::Input::FEEDBACK) {
				const dx7::Gains g = dx7::Operator::gains(unit.coefficients(), unit.state().op, len);
				FmOpKernel::compute_neon(outptr, simd_n, input == dx7::Input::BUS ? buf_[inbus - 1].get() : nullptr,
				                         param.phase, param.freq, g.gain1, g.dgain, add);
				unit.on<&dx7::Operator::skip>(len);
			}
			else {
				const int32_t* modulation = input == dx7::Input::BUS ? buf_[inbus - 1].get() : outptr;
				unit.renderBlock({modulation, len}, {outptr, len}, {outptr, len});
				if (input == dx7::Input::FEEDBACK) {
					fb_buf[0] = unit.state().loop.feedback.y0;
					fb_buf[1] = unit.state().loop.feedback.y1;
				}
			}
			has_contents[outbus] = true;
		}
		else {
			unit.on<&dx7::Operator::skip>(len);
			if (!add_flag) {
				has_contents[outbus] = false;
			}
		}
		param.phase = unit.state().op.phase;
		param.gain_out = unit.state().op.gain_out;
	}
}
