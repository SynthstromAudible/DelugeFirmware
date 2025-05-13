/*
 * Copyright (c) 2025 Synthstrom Audible Limited
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

#include "gui/menu_item/lfo/type.h"

#include "hid/display/oled.h"
#include "modulation/lfo.h"

namespace deluge::gui::menu_item::lfo {

void Type::getColumnLabel(StringBuf& label) {
	// We label with the LFO type name instead of "TYPE", since we draw the space underneath:
	// that way the label helps explain the shape.
	getShortOption(label);
}

void Type::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	renderColumnLabel(startX, width, startY);

	// Draw the LFO shape
	int32_t plotHeight = height - kTextSpacingY - 2;
	int32_t plotWidth = width - 2;
	int32_t baseline = OLED_MAIN_VISIBLE_HEIGHT - 1;
	LFOType wave = soundEditor.currentSound->lfoConfig[lfoId_].waveType;
	LFOConfig config{wave};
	LFO lfo;
	if (lfoId_ == 0) {
		lfo.setGlobalInitialPhase(config);
	}
	else {
		lfo.setLocalInitialPhase(config);
	}
	int32_t phaseIncrement;
	int32_t extraScaling = 1;
	if (wave == LFOType::RANDOM_WALK) {
		phaseIncrement = UINT32_MAX / (plotWidth / 9);
		// RANDOM walk range is smaller, so we magnify for display.
		extraScaling = 5;
	}
	else if (wave == LFOType::SAMPLE_AND_HOLD) {
		phaseIncrement = UINT32_MAX / (plotWidth / 9);
	}
	else if (wave == LFOType::WARBLER) {
		phaseIncrement = UINT32_MAX / (plotWidth / 9);
		// warbler is very peaky, so despite using the full range it's frequently filtered out by the resolution
		extraScaling = 15;
	}
	else {
		phaseIncrement = UINT32_MAX / (plotWidth / 2.4);
	}
	bool first = true;
	int32_t prevY = 0;
	float yScale = ((int64_t)INT32_MAX * 2) / plotHeight;
	for (size_t x = 0; x < plotWidth; x++) {
		int32_t yBig = lfo.render(1, wave, phaseIncrement) * extraScaling;
		int32_t y = (yBig / yScale) + plotHeight / 2;
		// We're subtracting the Y because the screen coordinates grow down.
		image.drawVerticalLine(startX + x, baseline - y - 1, baseline - y);
		if (!first) {
			int32_t delta = y - prevY;
			if (delta > 1) {
				image.drawVerticalLine(startX + x, baseline - y, baseline - prevY);
			}
			else if (delta < -1) {
				image.drawVerticalLine(startX + x, baseline - prevY, baseline - y);
			}
		}
		first = false;
		prevY = y;
	}
}

} // namespace deluge::gui::menu_item::lfo
