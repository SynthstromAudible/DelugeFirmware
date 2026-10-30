/*
 * Derived from Dexed's fm_core, under its licence:
 *
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

/*
 * Derived from Dexed's EngineMkI, under its licence:
 *
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
 */

/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "dsp-next/fixed/dx7/graph.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#ifdef DELUGE_DX7_NEON_KERNEL
/// The device's NEON modern operator (neon_fm_kernel.s, or a test-local copy under another name).
extern "C" void DELUGE_DX7_NEON_KERNEL(const int32_t* in, const int32_t* busin, int32_t* out, int count, int32_t phase0,
                                       int32_t freq, int32_t gain1, int32_t dgain);
#endif

namespace deluge::dsp::fixed::dx7 {

namespace {

#ifdef DELUGE_DX7_NEON_KERNEL
/// @brief Silence: the kernel's modulation for an op with no input, and its mix for an op that sets its output.
alignas(16) constexpr std::array<q31, kMaxBlock> kZeros{};

/// @brief The kernel's count for a block of @p n: a multiple of 4, and never 12k + 8, which it cannot take.
constexpr int kernel_count(std::size_t n) {
	const auto len = static_cast<int>(n);
	const int nmod = 1 + ((len + 11) % 12);
	return nmod == 8 ? len + 4 : (len + 3) & ~3;
}
#endif

} // namespace

void Graph::render_op(const Operator::Settings& settings, [[maybe_unused]] const Gains& gains, std::size_t op,
                      Signal modulation, Buffer target) {
	const std::size_t n = target.size();
	if (settings.engine == Engine::MODERN && settings.input != Input::FEEDBACK) {
#ifdef DELUGE_DX7_NEON_KERNEL
		// The kernel computes what ModernVectorOperator computes (tests/dsp/dx7_graph_spec.cpp holds them equal), with
		// its loads issued a block ahead, which the compiler cannot schedule for the vector step without spilling.
		DELUGE_DX7_NEON_KERNEL(settings.input == Input::NONE ? kZeros.data() : modulation.data(),
		                       settings.output == Output::ADD ? target.data() : kZeros.data(), target.data(),
		                       kernel_count(n), ops_[op].phase, settings.freq, gains.gain1, gains.dgain);
		hold_op(op, gains, settings.freq, n);
#else
		Mono<ModernVectorOperator> vector{{.input = settings.input,
		                                   .output = settings.output,
		                                   .level_in = settings.level_in,
		                                   .freq = settings.freq,
		                                   .tables = settings.tables}};
		vector.on<&ModernVectorOperator::resume>(ops_[op]);
		vector.render_block(modulation, target, target);
		ops_[op] = vector.state().op;
#endif
		return;
	}
	Mono<Operator> unit{settings};
	unit.on<&Operator::resume>(ops_[op], feedback_);
	unit.render_block(modulation, target, target);
	ops_[op] = unit.state().op;
	if (settings.input == Input::FEEDBACK) {
		feedback_ = unit.state().loop.feedback;
	}
}

void Graph::render_block(const Settings& s, Buffer out, Buses& lent) {
	const std::size_t n = out.size();
	assert(n > 0 && n <= kMaxBlock);
	const bool mki = s.engine == Engine::MKI;
	constexpr int32_t kFeedbackBits = 8;
	const int32_t shift = s.feedback != 0 ? kFeedbackBits - s.feedback : 16;
	const bool feedback_on = shift < 16;

	std::array<uint8_t, 6> ops = kAlgorithms[s.algorithm];
	if (mki && feedback_on && (s.algorithm == 3 || s.algorithm == 5)) {
		ops[0] = kMkIFusedFeedback; // MkI fuses op 6's stack in algorithms 4 and 6
	}

	std::array<bool, 3> has_contents{true, false, false};
	const std::array<q31*, 3> buses{out.data(), lent.bus[0].data(), lent.bus[1].data()};
	for (std::size_t op = 0; op < 6; ++op) {
		const uint8_t flags = ops[op];
		bool add = has(flags, Flag::OUT_BUS_ADD);
		const int inbus = (flags >> 4) & 3;
		const int outbus = flags & 3;
		const Operator::Settings probe{.engine = s.engine,
		                               .level_in = s.level_in[op],
		                               .freq = s.freq[op],
		                               .feedback_shift = shift,
		                               .tables = s.tables};
		const Operator::Coefficients coefficients = Operator::coefficients(probe);
		const Gains gains = Operator::gains(coefficients, ops_[op], n);
		if (!Operator::audible(coefficients, gains)) {
			hold_op(op, gains, probe.freq, n); // the gate
			if (!add) {
				has_contents[outbus] = false;
			}
			continue;
		}
		if (!has_contents[outbus]) {
			add = false; // adding to an empty bus sets it
		}
		Operator::Settings settings = probe;
		settings.output = add ? Output::ADD : Output::SET;
		const Buffer target{buses[outbus], n};
		if (inbus == 0 || !has_contents[inbus]) { // an empty input is no input
			if (has(flags, Flag::FB_IN) && has(flags, Flag::FB_OUT) && feedback_on) {
				if (mki && s.algorithm == 3) {
					render_stack<3>(s, op, shift, target);
					op += 2;
				}
				else if (mki && s.algorithm == 5) {
					render_stack<2>(s, op, shift, target);
					op += 1;
				}
				else {
					settings.input = Input::FEEDBACK;
					if (mki && s.algorithm == 31) {
						settings.feedback_shift = std::min(shift + 2, int32_t{16}); // MkI's algorithm 32
					}
					render_op(settings, gains, op, target, target);
				}
			}
			else {
				render_op(settings, gains, op, target, target);
			}
		}
		else {
			settings.input = Input::BUS;
			render_op(settings, gains, op, Signal{buses[inbus], n}, target);
		}
		has_contents[outbus] = true;
	}
}

} // namespace deluge::dsp::fixed::dx7
