// Based on Dexed's env (Copyright 2017 Pascal Gauthier, Copyright 2012 Google Inc., Apache License 2.0).

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

#include "dsp-next/fixed/dx7/envelope.h"

#include "dsp-next/core/wrapping.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace deluge::dsp::fixed::dx7 {

q31 Envelope::advance(const Coefficients& c, State& s, std::size_t n) {
	const auto count = static_cast<int32_t>(n);
	if (s.hold != 0) {
		s.hold = wrapping_sub(s.hold, count);
		if (s.hold <= 0) {
			s.hold = 0;
			enter_stage(c, s, s.stage + 1, c.extra_rate);
		}
	}

	if ((s.stage < 3 || (s.stage < 4 && !s.down)) && s.hold == 0) {
		if (s.rising) {
			s.level = std::max(s.level, kJumpTarget << 16);
			// The slope is the doublings left below 17, 0 to 16, so the climb flattens near the top.
			const int32_t slope = wrapping_sub(17 << 24, s.level) >> 24;
			const auto step = static_cast<int32_t>(static_cast<uint32_t>(slope) * static_cast<uint32_t>(s.increment)
			                                       * static_cast<uint32_t>(count));
			s.level = wrapping_add(s.level, step);
			if (s.level >= s.target) {
				s.level = s.target;
				enter_stage(c, s, s.stage + 1, c.extra_rate);
			}
		}
		else {
			const auto step = static_cast<int32_t>(static_cast<uint32_t>(s.increment) * static_cast<uint32_t>(count));
			s.level = wrapping_sub(s.level, step);
			if (s.level <= s.target) {
				s.level = s.target;
				enter_stage(c, s, s.stage + 1, c.extra_rate);
			}
		}
	}
	return s.level;
}

int32_t Envelope::stage_target(int32_t level, int32_t output_level) {
	int32_t microsteps = (scale_output_level(level) >> 1) << 6;
	microsteps = wrapping_sub(wrapping_add(microsteps, output_level), kLevelOffset);
	return std::max(microsteps, 16) << 16;
}

void Envelope::enter_stage(const Coefficients& c, State& s, int32_t stage, int32_t extra_rate) {
	s.stage = stage;
	if (stage >= 4) {
		return;
	}
	const int32_t level = c.params.levels[stage];
	s.target = stage_target(level, c.output_level);
	s.rising = s.target > s.level;

	const int32_t rate = c.params.rates[stage];
	const int32_t qrate = std::min(((rate * 41) >> 6) + c.rate_scaling + extra_rate, 63);

	const bool attack_to_zero = stage == 0 && level == 0;
	if (s.target == s.level || attack_to_zero) {
		const int32_t static_rate = std::min(rate + c.rate_scaling + extra_rate, 99);
		s.hold = static_rate < 77 ? kStatics[static_rate] : 20 * (99 - static_rate);
		if (static_rate < 77 && attack_to_zero) {
			s.hold /= 20; // the attack is held a twentieth as long
		}
	}
	else {
		s.hold = 0;
	}

	// The DX7's rates are measured at 44.1 kHz, the engine's only rate, so no sample-rate scaling applies. qrate is 0
	// to 63 (the rate, scaling and extra rate are none of them negative), so the shift is 2 to 17.
	s.increment = (4 + (qrate & 3)) << (2 + (qrate >> 2));
}

} // namespace deluge::dsp::fixed::dx7
