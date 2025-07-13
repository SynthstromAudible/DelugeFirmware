/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#pragma once
#include "gui/menu_item/patch_cable_strength/fixed.h"
#include "processing/engines/audio_engine.h"

namespace deluge::gui::menu_item::sidechain {

class VolumeShortcut final : public patch_cable_strength::Fixed {
public:
	using Fixed::Fixed;
	void writeCurrentValue() override {
		Fixed::writeCurrentValue();
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}

	bool showColumnLabel() const override { return false; }

	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) override {
		oled_canvas::Canvas& image = OLED::main;

		constexpr int32_t barWidth = 26;
		constexpr int32_t dotsInterval = 4;
		constexpr int32_t xOffset = 4;

		const int32_t leftPadding = (width - barWidth) / 2;
		const int32_t minX = startX + leftPadding;
		const int32_t maxX = minX + barWidth;
		const int32_t minY = startY + 3;
		const int32_t maxY = startY + height - 4;

		// Draw the dots at left and right sides
		for (int32_t y = minY; y <= maxY; y += dotsInterval) {
			if (y == minY + dotsInterval * 3) {
				// small offset for better balance
				y++;
			}
			image.drawPixel(minX + xOffset, y);
			image.drawPixel(maxX - xOffset, y);
		}

		// Calculate shape height based on value
		const float normalizedValue = std::abs(getValue()) / 5000.0f;
		const int32_t barHeight = maxY - minY;
		const int32_t fillHeight = static_cast<int32_t>(normalizedValue * barHeight);

		const int32_t yOffset = (barHeight - fillHeight) / 2;
		int32_t yStart, yEnd, y0, y1;
		if (getValue() >= 0) {
			// For positive values, draw from top down
			yStart = minY + yOffset;
			yEnd = yStart + fillHeight;
			y0 = yEnd;
			y1 = yStart;
		}
		else {
			// For negative values, draw from bottom up
			yEnd = maxY - yOffset;
			yStart = yEnd - fillHeight;
			y0 = yStart;
			y1 = yEnd;
		}

		// Draw a sidechain level shape
		image.drawLine(minX + xOffset, y0, maxX - xOffset, y1, true);
		image.drawVerticalLine(minX + xOffset, yStart, yEnd);
		image.drawVerticalLine(minX + xOffset + 1, yStart, yEnd);
		image.drawHorizontalLine(y1 - 1, maxX - xOffset, maxX - 1);
		image.drawHorizontalLine(y1, maxX - xOffset, maxX - 1);
		image.drawHorizontalLine(y1 - 1, minX + 1, minX + xOffset + 1);
		image.drawHorizontalLine(y1, minX + 1, minX + xOffset + 1);
	}
};

} // namespace deluge::gui::menu_item::sidechain
