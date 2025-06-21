/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "number.h"
#include "hid/display/oled.h"

namespace deluge::gui::menu_item {

float Number::getValuePercent() {
	const int32_t minValue = getMinValue();
	const int32_t maxValue = getMaxValue();
	return static_cast<float>(getValue() - minValue) / static_cast<float>(maxValue - minValue);
}

void Number::drawBar(int32_t yTop, int32_t marginL, int32_t marginR) {
	hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;
	if (marginR == -1) {
		marginR = marginL;
	}
	constexpr int32_t height = 7;

	const int32_t leftMost = marginL;
	const int32_t rightMost = OLED_MAIN_WIDTH_PIXELS - marginR - 1;

	const int32_t minValue = getMinValue();
	const int32_t maxValue = getMaxValue();
	const uint32_t range = maxValue - minValue;
	const float posFractional = static_cast<float>(this->getValue() - minValue) / range;
	const float zeroPosFractional = static_cast<float>(-minValue) / range;

	const int32_t width = rightMost - leftMost;
	const int32_t posHorizontal = static_cast<int32_t>(posFractional * width + 0.5);

	if (int32_t zeroPosHorizontal = static_cast<int32_t>(zeroPosFractional * width);
	    posHorizontal <= zeroPosHorizontal) {
		const int32_t xMin = leftMost + posHorizontal;
		canvas.invertArea(xMin, zeroPosHorizontal - posHorizontal + 1, yTop, yTop + height);
	}
	else {
		const int32_t xMin = leftMost + zeroPosHorizontal;
		canvas.invertArea(xMin, posHorizontal - zeroPosHorizontal, yTop, yTop + height);
	}
	canvas.drawRectangle(leftMost, yTop, rightMost, yTop + height);
}

void Number::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	hid::display::oled_canvas::Canvas& image = hid::display::OLED::main;

	const auto numberStyle = getNumberStyle();

	if (numberStyle == NUMBER) {
		DEF_STACK_STRING_BUF(paramValue, 10);
		paramValue.appendInt(getValue());
		return image.drawStringCentered(paramValue, startX, startY + 3, kTextTitleSpacingX, kTextTitleSizeY, width);
	}
	if (numberStyle == PERCENT) {
		return drawPercent(startX, startY, width, height);
	}

	if (numberStyle == KNOB) {
		return drawKnob(startX, startY, width, height);
	}

	if (numberStyle == VERTICAL_BAR) {
		return drawVerticalBar(startX, startY, width, height);
	}
}

void Number::drawPercent(int32_t startX, int32_t startY, int32_t width, int32_t height) {
	hid::display::oled_canvas::Canvas& image = hid::display::OLED::main;

	DEF_STACK_STRING_BUF(valueString, 12);
	const int32_t percents = getValuePercent() * 100;
	valueString.appendInt(percents);

	constexpr char percentChar = '%';
	const int32_t valueWidth = image.getStringWidthInPixels(valueString.c_str(), kTextSpacingY);
	const int32_t percentCharWidth = image.getCharWidthInPixels(percentChar, kTextSpacingY);
	constexpr int32_t paddingBetween = 2;
	const int32_t totalWidth = valueWidth + percentCharWidth + paddingBetween;

	int32_t x = startX + (width - totalWidth) / 2 - 1;
	int32_t y = startY + 3;
	image.drawString(valueString.c_str(), x, y, kTextSpacingX, kTextSpacingY);

	x += valueWidth + paddingBetween;
	image.drawChar(percentChar, x, y, kTextSpacingX, kTextSpacingY);
}

void Number::drawKnob(int32_t startX, int32_t startY, int32_t width, int32_t height) {
	hid::display::oled_canvas::Canvas& image = hid::display::OLED::main;

	// Draw the background arc
	// Easier to adjust pixel-perfect, so we use a bitmap
	const auto& arcIcon = hid::display::OLED::knobArcIcon;
	const int32_t arcIconWidth = arcIcon.size() / 2;
	const int32_t leftPadding = ((width - arcIconWidth) / 2) - 1;
	image.drawGraphicMultiLine(arcIcon.data(), startX + leftPadding, startY, arcIconWidth, 16, 2);

	// Calculate current value angle
	constexpr int32_t knobRadius = 10;
	const int32_t centerX = (startX + width / 2) - 2;
	const int32_t centerY = startY + knobRadius + 1;
	constexpr int32_t arcRangeAngle = 210, beginningAngle = 165;
	const float valuePercent = getValuePercent();
	const float currentAngle = beginningAngle + (arcRangeAngle * valuePercent);

	// Draw the indicator line
	const float radians = (currentAngle * M_PI) / 180.0f;
	constexpr float innerRadius = knobRadius - 1.0f;
	constexpr float outerRadius = 3.5f;
	const float cosA = cos(radians);
	const float sinA = sin(radians);
	const float xStart = centerX + outerRadius * cosA;
	const float yStart = centerY + outerRadius * sinA;
	const float xEnd = centerX + innerRadius * cosA;
	const float yEnd = centerY + innerRadius * sinA;
	image.drawLine(round(xStart), round(yStart), round(xEnd), round(yEnd), true);

	// If the knob's position is near left or near right, fill the gap on the bitmap (the gap exists purely for
	// stylistic effect)
	if (currentAngle < 180.0f) {
		image.drawPixel(centerX - knobRadius, startY + height - 4);
	}
	if (currentAngle > 360.0f) {
		image.drawPixel(centerX + knobRadius, startY + height - 4);
	}
}

void Number::drawVerticalBar(int32_t startX, int32_t startY, int32_t slotWidth, int32_t slotHeight) {
	hid::display::oled_canvas::Canvas& image = hid::display::OLED::main;

	constexpr int32_t barWidth = 18;

	constexpr int32_t dotsInterval = 3;
	constexpr int32_t fillPadding = 3;
	const int32_t leftPadding = ((slotWidth - barWidth) / 2) - 2;

	const int32_t minX = startX + leftPadding;
	const int32_t maxX = minX + barWidth;
	const int32_t minY = startY + 1;
	const int32_t maxY = minY + slotHeight - 4;
	const int32_t barHeight = maxY - minY;

	// Draw the dots at left and right sides
	// 1px offset from the bottom is better than 1px offset from the top
	for (int32_t y = minY; y <= maxY; y += dotsInterval) {
		image.drawPixel(minX, y);
		image.drawPixel(maxX, y);
	}

	// Calculate fill height based on value
	const float valuePercent = getValuePercent();
	const int32_t fillHeight = static_cast<int32_t>(valuePercent * barHeight);
	if (fillHeight != barHeight) {
		// Draw the top dots
		for (int32_t x = minX + fillPadding; x < minX + barWidth; x += dotsInterval) {
			image.drawPixel(x, minY);
		}
	}
	if (fillHeight == 0) {
		// Draw the bottom dots
		for (int32_t x = minX + fillPadding; x < minX + barWidth; x += dotsInterval) {
			image.drawPixel(x, maxY);
		}
	}

	if (fillHeight > 0) {
		// Fill from bottom to value
		image.invertArea(minX + fillPadding, barWidth - fillPadding - 2, maxY - fillHeight, maxY);
	}
}

} // namespace deluge::gui::menu_item
