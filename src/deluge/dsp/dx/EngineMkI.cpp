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
#include <cmath>
#include <cstdlib>

#include "dsp-next/core/mono.h"
#include "dsp-next/fixed/dx7/operator.h"
#include "engine.h"
#include "math_lut.h"
#include <algorithm>
#include <array>
#include <cstddef>

using std::max;
using std::min;

namespace dx7 = deluge::dsp::fixed::dx7;

static const uint16_t ENV_BITDEPTH = 14;
const uint16_t ENV_MAX = 1 << ENV_BITDEPTH;

inline int32_t mkiSin(int32_t phase, uint16_t env) {
	return deluge::dsp::fixed::dx7::mki_sin(dxEngine->tables, phase, env);
}

// exclusively used for ALGO 6 with feedback
void EngineMkI::compute_fb2(int32_t* output, int n, FmOpParams* parms, int32_t gain01, int32_t gain02, int32_t dgain0,
                            int32_t* fb_buf, int fb_shift) {
	int32_t dgain[2];
	int32_t gain[2];
	int32_t phase[2];
	int32_t y0 = fb_buf[0];
	int32_t y = fb_buf[1];

	phase[0] = parms[0].phase;
	phase[1] = parms[1].phase;

	parms[1].gain_out = (ENV_MAX - (parms[1].level_in >> (28 - ENV_BITDEPTH)));

	gain[0] = gain01;
	gain[1] = parms[1].gain_out == 0 ? (ENV_MAX - 1) : parms[1].gain_out;

	dgain[0] = dgain0;
	dgain[1] = (parms[1].gain_out - (parms[1].gain_out == 0 ? (ENV_MAX - 1) : parms[1].gain_out));

	for (int i = 0; i < n; i++) {
		int32_t scaled_fb = (y0 + y) >> (fb_shift + 1);

		// op 0
		gain[0] += dgain[0];
		y0 = y;
		y = mkiSin(phase[0] + scaled_fb, gain[0]);
		phase[0] += parms[0].freq;

		// op 1
		gain[1] += dgain[1];
		y = mkiSin(phase[1] + y, gain[1]);
		phase[1] += parms[1].freq;

		output[i] = y;
	}
	fb_buf[0] = y0;
	fb_buf[1] = y;
}

// exclusively used for ALGO 4 with feedback
void EngineMkI::compute_fb3(int32_t* output, int n, FmOpParams* parms, int32_t gain01, int32_t gain02, int32_t dgain0,
                            int32_t* fb_buf, int fb_shift) {
	int32_t dgain[3];
	int32_t gain[3];
	int32_t phase[3];
	int32_t y0 = fb_buf[0];
	int32_t y = fb_buf[1];

	phase[0] = parms[0].phase;
	phase[1] = parms[1].phase;
	phase[2] = parms[2].phase;

	parms[1].gain_out = (ENV_MAX - (parms[1].level_in >> (28 - ENV_BITDEPTH)));
	parms[2].gain_out = (ENV_MAX - (parms[2].level_in >> (28 - ENV_BITDEPTH)));

	gain[0] = gain01;
	gain[1] = parms[1].gain_out == 0 ? (ENV_MAX - 1) : parms[1].gain_out;
	gain[2] = parms[2].gain_out == 0 ? (ENV_MAX - 1) : parms[2].gain_out;

	dgain[0] = dgain0;
	dgain[1] = (parms[1].gain_out - (parms[1].gain_out == 0 ? (ENV_MAX - 1) : parms[1].gain_out));
	dgain[2] = (parms[2].gain_out - (parms[2].gain_out == 0 ? (ENV_MAX - 1) : parms[2].gain_out));

	for (int i = 0; i < n; i++) {
		int32_t scaled_fb = (y0 + y) >> (fb_shift + 1);

		// op 0
		gain[0] += dgain[0];
		y0 = y;
		y = mkiSin(phase[0] + scaled_fb, gain[0]);
		phase[0] += parms[0].freq;

		// op 1
		gain[1] += dgain[1];
		y = mkiSin(phase[1] + y, gain[1]);
		phase[1] += parms[1].freq;

		// op 2
		gain[2] += dgain[2];
		y = mkiSin(phase[2] + y, gain[2]);
		phase[2] += parms[2].freq;

		output[i] = y;
	}
	fb_buf[0] = y0;
	fb_buf[1] = y;
}

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
		// PG: this is my 'dirty' implementation of FB for 2 and 3 operators...
		const bool feedback = (flags & 0xc0) == 0xc0 && fb_on;
		const bool fused = feedback && (algorithm == 3 || algorithm == 5);
		dx7::Input input = dx7::Input::BUS;
		if (inbus == 0 || !has_contents[inbus]) {
			input = feedback ? dx7::Input::FEEDBACK : dx7::Input::NONE;
		}
		// one operator feedback, process exception for ALGO 32
		const int32_t op_shift = algorithm == 31 ? min((feedback_shift + 2), (int32_t)16) : feedback_shift;

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
				const dx7::Gains g = dx7::Operator::gains(unit.coefficients(), unit.state().op, len);
				param.gain_out = g.gain2;
				if (algorithm == 3) {
					// three operator feedback, process exception for ALGO 4
					compute_fb3(outptr, n, params, g.gain1, g.gain2, g.dgain, fb_buf,
					            min((feedback_shift + 2), (int32_t)16));
					params[1].phase += params[1].freq * n; // hack, we already processed op-5 - op-4
					params[2].phase += params[2].freq * n; // yuk yuk
				}
				else {
					// two operator feedback, process exception for ALGO 6
					compute_fb2(outptr, n, params, g.gain1, g.gain2, g.dgain, fb_buf,
					            min((feedback_shift + 2), (int32_t)16));
					params[1].phase += params[1].freq * n; // yuk, hack, we already processed op-5
				}
				unit.on<&dx7::Operator::skip>(len);
				param.phase = unit.state().op.phase;
				op += algorithm == 3 ? 2 : 1; // ignore the other operators of the stack
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
