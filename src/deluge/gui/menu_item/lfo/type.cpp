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

using namespace deluge::hid::display;

namespace deluge::gui::menu_item::lfo {

void Type::getColumnLabel(StringBuf& label) {
	// We label with the LFO type name instead of "TYPE", since we draw the space underneath:
	// that way the label helps explain the shape.
	getShortOption(label);
}

void Type::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	oled_canvas::Canvas& image = OLED::main;
	renderColumnLabel(startX, width, startY);

	const uint8_t* bitmap = getLfoIconBitmap();
	image.drawGraphicMultiLine(bitmap, startX + 1, startY + kTextSpacingX + 3, 32, 16,
	                           2); // Note numBytesTall = 2
}

const uint8_t* Type::getLfoIconBitmap() {
	switch (soundEditor.currentSound->lfoConfig[lfoId_].waveType) {
	case LFOType::SINE:
		return OLED::lfoIconSine;
	case LFOType::TRIANGLE:
		return OLED::lfoIconTriangle;
	case LFOType::SQUARE:
		return OLED::lfoIconSquare;
	case LFOType::SAW:
		return OLED::lfoIconSaw;
	case LFOType::SAMPLE_AND_HOLD:
		return OLED::lfoIconSampleHold;
	case LFOType::RANDOM_WALK:
		return OLED::lfoIconRandomWalk;
	case LFOType::WARBLER:
		return OLED::lfoIconWarbler;
	}
	return OLED::lfoIconSine;
}

} // namespace deluge::gui::menu_item::lfo
