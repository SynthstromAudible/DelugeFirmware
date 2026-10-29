/*
 * Copyright (C) 2015-2017 Pascal Gauthier.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 *
 * The code is based on ppplay https://github.com/stohrendorf/ppplay and opl3
 * math documentation :
 * https://github.com/gtaylormb/opl3_fpga/blob/master/docs/opl3math/opl3math.pdf
 *
 */

#include "EngineMkI.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/fixed/dx7/mki_feedback_stack.h"
#include "dsp-next/fixed/dx7/operator.h"
#include "engine.h"
#include <algorithm>
#include <array>
#include <cstddef>

namespace dx7 = deluge::dsp::fixed::dx7;

namespace {

/// @brief MkI's feedback stack of N ops from op 0, rendered into @p output, the ops' state and the feedback history
/// written back.
template <std::size_t N>
void render_stack(int32_t* output, std::size_t n, FmOpParams* params, int32_t* fb_buf, int32_t feedback_shift) {
	using Stack = dx7::MkIFeedbackStack<N>;
	typename Stack::Settings settings{.feedback_shift = feedback_shift, .tables = &dxEngine->tables};
	std::array<dx7::OperatorState, N> ops{};
	for (std::size_t k = 0; k < N; ++k) {
		settings.level_in[k] = params[k].level_in;
		settings.freq[k] = params[k].freq;
		ops[k] = {.phase = params[k].phase, .gain_out = params[k].gain_out};
	}
	deluge::dsp::Mono<Stack> stack{settings};
	stack.template on<&Stack::resume>(ops, dx7::FeedbackHistory{.y0 = fb_buf[0], .y1 = fb_buf[1]});
	stack.renderBlock({output, n});
	for (std::size_t k = 0; k < N; ++k) {
		params[k].phase = stack.state().op[k].phase;
		params[k].gain_out = stack.state().op[k].gain_out;
	}
	fb_buf[0] = stack.state().loop.feedback.y0;
	fb_buf[1] = stack.state().loop.feedback.y1;
}

} // namespace

void EngineMkI::render(int32_t* output, int n, FmOpParams* params, int algorithm, int32_t* fb_buf,
                       int32_t feedback_shift) {
	auto alg = std::to_array(dx7::kAlgorithms[algorithm]);
	bool has_contents[3] = {true, false, false};
	bool fb_on = feedback_shift < 16;
	const auto len = static_cast<std::size_t>(n);

	switch (algorithm) {
	case 3:
	case 5:
		if (fb_on)
			alg[0] = dx7::kMkIFusedFeedback;
	}

	for (int op = 0; op < 6; op++) {
		const int flags = alg[op];
		const bool add_flag = (flags & dx7::OUT_BUS_ADD) != 0;
		FmOpParams& param = params[op];
		const int inbus = (flags >> 4) & 3;
		const int outbus = flags & 3;
		int32_t* outptr = (outbus == 0) ? output : buf_[outbus - 1].get();
		const bool add = add_flag && has_contents[outbus];
		// algorithms 4 and 6 with feedback render op 6's whole stack at once
		const bool feedback = (flags & 0xc0) == 0xc0 && fb_on;
		const bool fused = feedback && (algorithm == 3 || algorithm == 5);
		dx7::Input input = dx7::Input::BUS;
		if (inbus == 0 || !has_contents[inbus]) {
			input = feedback ? dx7::Input::FEEDBACK : dx7::Input::NONE;
		}
		// one operator feedback, process exception for ALGO 32
		const int32_t op_shift = algorithm == 31 ? std::min(feedback_shift + 2, int32_t{16}) : feedback_shift;

		deluge::dsp::Mono<dx7::Operator> unit{{.engine = dx7::Engine::MKI,
		                                       .input = input,
		                                       .output = add ? dx7::Output::ADD : dx7::Output::SET,
		                                       .level_in = param.level_in,
		                                       .freq = param.freq,
		                                       .feedback_shift = op_shift,
		                                       .tables = &dxEngine->tables}};
		unit.on<&dx7::Operator::resume>(dx7::OperatorState{.phase = param.phase, .gain_out = param.gain_out},
		                                dx7::FeedbackHistory{.y0 = fb_buf[0], .y1 = fb_buf[1]});

		if (dx7::Operator::audible(unit.coefficients(), unit.state().op, len)) {
			if (input == dx7::Input::FEEDBACK && fused) {
				// the stack renders op 6 and the ops it feeds, so the loop skips them
				if (algorithm == 3) {
					render_stack<3>(outptr, len, params, fb_buf, feedback_shift);
					op += 2;
				}
				else {
					render_stack<2>(outptr, len, params, fb_buf, feedback_shift);
					op += 1;
				}
				has_contents[outbus] = true;
				continue;
			}
			const int32_t* modulation = input == dx7::Input::BUS ? buf_[inbus - 1].get() : outptr;
			unit.renderBlock({modulation, len}, {outptr, len}, {outptr, len});
			if (input == dx7::Input::FEEDBACK) {
				fb_buf[0] = unit.state().loop.feedback.y0;
				fb_buf[1] = unit.state().loop.feedback.y1;
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
