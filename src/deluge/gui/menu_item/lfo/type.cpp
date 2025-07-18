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

void Type::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	oled_canvas::Canvas& image = OLED::main;

	const LFOType type = soundEditor.currentSound->lfoConfig[lfoId_].waveType;
	const auto& bitmap = getLfoIconBitmap(type);
	image.drawGraphicMultiLine(bitmap.data(), startX + 3, startY + 4, 25, 16, 2);
}

const std::vector<uint8_t>& Type::getLfoIconBitmap(LFOType type) {
	switch (type) {
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
	return OLED::lfoIconSine; // satisfies -Wreturn-type
}

} // namespace deluge::gui::menu_item::lfo
