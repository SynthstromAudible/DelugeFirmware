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

#include "ui/widgets/grid/shortcut_blink.h"

#include <algorithm>

namespace deluge::ui::widgets::grid {

bool BlinkPlan::any_source() const {
	return std::ranges::any_of(sources, [](const auto& column) {
		return std::ranges::any_of(column, [](const SourceBlink& s) { return s.freq_mask != kNoBlink; });
	});
}

BlinkPlan exclusive_blink(uint8_t x, uint8_t y, uint8_t freq_mask) {
	BlinkPlan plan;
	plan.param = ParamBlink{.x = x, .y = y, .valid = true, .freq_mask = freq_mask, .colour = FlashColour::Default};
	return plan;
}

uint32_t blink_step(uint32_t anchor_ms, uint32_t now_ms) {
	const uint32_t d = now_ms - anchor_ms;
	return 2 * (d / kBlinkCycleMs) + ((d % kBlinkCycleMs) >= kSourceBlinkMs ? 1 : 0);
}

uint32_t ms_to_next_step(uint32_t anchor_ms, uint32_t now_ms) {
	const uint32_t phase = (now_ms - anchor_ms) % kBlinkCycleMs;
	return phase < kSourceBlinkMs ? (kSourceBlinkMs - phase) : (kBlinkCycleMs - phase);
}

void fire_blink_step(GridFrame& f, const BlinkPlan& plan, uint32_t step) {
	const uint32_t cycle = step >> 1;
	if ((step & 1) != 0) {
		// The param step, 180 ms long.
		if (plan.param.valid && (cycle & plan.param.freq_mask) == 0) {
			f.flash(plan.param.x, plan.param.y, plan.param.colour);
		}
		return;
	}
	// The source step, 20 ms long.
	for (int32_t sx = 0; sx < 2; sx++) {
		for (int32_t y = 0; y < kBlinkSourceRows; y++) {
			const SourceBlink& s = plan.sources[sx][y];
			if (s.freq_mask == kNoBlink || (cycle & s.freq_mask) != 0) {
				continue;
			}
			FlashColour colour = s.colour;
			// The next counter bit above the mask alternates the flash to the second layer's yellow.
			if (s.alternates && (cycle & (uint32_t{s.freq_mask} + 1)) != 0) {
				colour = FlashColour::Yellow;
			}
			f.flash(kBlinkSourceCol + sx, y, colour);
		}
	}
}

} // namespace deluge::ui::widgets::grid
