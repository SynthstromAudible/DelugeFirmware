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
	const std::vector<uint8_t>& bitmap = getLfoIconBitmap(type);
	const uint8_t bitmapXOffset = getLfoIconBitmapXOffset(type);

	constexpr uint8_t numBytesTall = 2;
	constexpr uint8_t bitmapHeight = 16;
	const uint8_t bitmapWidth = bitmap.size() / numBytesTall;

	uint8_t currentX = startX + 3;
	uint8_t currentOffset = bitmapXOffset;
	uint8_t endX = startX + width - 7;

	// Draw looped lfo shape image until it fits the width
	while (currentX < endX) {
		uint8_t remaining = endX - currentX;
		uint8_t drawWidth = bitmapWidth - currentOffset;
		if (drawWidth > remaining) {
			drawWidth = remaining;
		}

		image.drawGraphicMultiLine(bitmap.data() + currentOffset * numBytesTall, currentX, startY + 5, drawWidth,
		                           bitmapHeight, numBytesTall);
		currentX += drawWidth;
		currentOffset = 0; // After the first draw, always start from 0 of the bitmap
	}
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

const uint8_t Type::getLfoIconBitmapXOffset(LFOType type) {
	switch (type) {
	case LFOType::SQUARE:
		return 3;
	case LFOType::SAW:
		return 10;
	default:
		return 1;
	}
}

} // namespace deluge::gui::menu_item::lfo
