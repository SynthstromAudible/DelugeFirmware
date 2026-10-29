// Based on Dexed's fm_core (Copyright 2012 Google Inc., Apache License 2.0) and EngineMkI (Copyright (C) 2015-2017
// Pascal Gauthier, GPLv3).

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
#include <cstddef>
#include <cstdint>

namespace deluge::dsp::fixed::dx7 {

void Graph::renderBlock(const Settings& s, Buffer out, Buses& lent) {
	const std::size_t n = out.size();
	const bool mki = s.engine == Engine::MKI;
	constexpr int32_t kFeedbackBits = 8;
	const int32_t shift = s.feedback != 0 ? kFeedbackBits - s.feedback : 16;
	const bool feedback_on = shift < 16;

	std::array<uint8_t, 6> ops = std::to_array(kAlgorithms[s.algorithm]);
	if (mki && feedback_on && (s.algorithm == 3 || s.algorithm == 5)) {
		ops[0] = kMkIFusedFeedback; // MkI fuses op 6's stack in algorithms 4 and 6
	}

	std::array<bool, 3> has_contents{true, false, false};
	const std::array<q31*, 3> buses{out.data(), lent.bus[0].data(), lent.bus[1].data()};
	for (std::size_t op = 0; op < 6; ++op) {
		const uint8_t flags = ops[op];
		bool add = (flags & OUT_BUS_ADD) != 0;
		const int inbus = (flags >> 4) & 3;
		const int outbus = flags & 3;
		const Operator::Settings probe{.engine = s.engine,
		                               .level_in = s.level_in[op],
		                               .freq = s.freq[op],
		                               .feedback_shift = shift,
		                               .tables = s.tables};
		if (!Operator::audible(Operator::coefficients(probe), ops_[op], n)) {
			skip_op(probe, op, n); // the gate
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
			if ((flags & (FB_IN | FB_OUT)) == (FB_IN | FB_OUT) && feedback_on) {
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
					render_op(settings, op, target, target);
				}
			}
			else {
				render_op(settings, op, target, target);
			}
		}
		else {
			settings.input = Input::BUS;
			render_op(settings, op, Signal{buses[inbus], n}, target);
		}
		has_contents[outbus] = true;
	}
}

} // namespace deluge::dsp::fixed::dx7
