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
#include "trigger/out/ppqn.h"
#include "util/functions.h"

namespace deluge::gui::menu_item {

using namespace hid::display;

float Number::getNormalizedValue() {
	const int32_t minValue = getMinValue();
	const int32_t maxValue = getMaxValue();
	return static_cast<float>(getValue() - minValue) / static_cast<float>(maxValue - minValue);
}

void Number::drawHorizontalBar(int32_t yTop, int32_t marginL, int32_t marginR, int32_t height) {
	oled_canvas::Canvas& canvas = OLED::main;
	if (marginR == -1) {
		marginR = marginL;
	}
	const int32_t leftMost = marginL + 2;
	const int32_t rightMost = OLED_MAIN_WIDTH_PIXELS - marginR - 3;

	const int32_t value = getValue();
	const int32_t minValue = getMinValue();
	if (minValue == 0 && value == 0) {
		return canvas.drawRectangleRounded(leftMost, yTop, rightMost - 1, yTop + height);
	}

	const int32_t maxValue = getMaxValue();
	const uint32_t range = maxValue - minValue;
	const float posFractional = static_cast<float>(value - minValue) / range;
	const float zeroPosFractional = static_cast<float>(-minValue) / range;

	const int32_t width = rightMost - leftMost;
	const int32_t posHorizontal = static_cast<int32_t>(posFractional * width);

	if (const int32_t zeroPosHorizontal = static_cast<int32_t>(zeroPosFractional * width);
	    posHorizontal == zeroPosHorizontal) {
		// = 0, draw vertical line in the middle
		const int32_t xMin = leftMost + zeroPosHorizontal;
		canvas.invertArea(xMin, 1, yTop + 1, yTop + height - 1);
	}
	else if (posHorizontal < zeroPosHorizontal) {
		// < 0, draw the bar to the left
		const int32_t xMin = leftMost + posHorizontal;
		canvas.invertArea(xMin, zeroPosHorizontal - posHorizontal, yTop + 1, yTop + height - 1);
	}
	else {
		// > 0, draw the bar to the right
		const int32_t xMin = leftMost + zeroPosHorizontal;
		canvas.invertArea(xMin, posHorizontal - zeroPosHorizontal, yTop + 1, yTop + height - 1);
	}

	canvas.drawRectangleRounded(leftMost, yTop, rightMost - 1, yTop + height);
}

void Number::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	switch (getNumberStyle()) {
	case PERCENT:
		return drawPercent(startX, startY, width, height);
	case KNOB:
		return drawKnob(startX, startY, width, height);
	case BAR:
		return drawBar(startX, startY, width, height);
	case SLIDER:
		return drawSlider(startX, startY, width, height);
	case LENGTH_SLIDER:
		return drawLengthSlider(startX, startY, width, height);
	default:
		DEF_STACK_STRING_BUF(paramValue, 10);
		paramValue.appendInt(getValue());
		return OLED::main.drawStringCentered(paramValue, startX, startY + 3, kTextTitleSpacingX, kTextTitleSizeY,
		                                     width);
	}
}

void Number::drawPercent(int32_t startX, int32_t startY, int32_t width, int32_t height) {
	oled_canvas::Canvas& image = OLED::main;

	DEF_STACK_STRING_BUF(valueString, 12);
	valueString.appendInt(getValue() * 2);

	constexpr char percentChar = '%';
	const int32_t valueWidth = image.getStringWidthInPixels(valueString.c_str(), kTextSpacingY);
	const int32_t percentCharWidth = image.getCharWidthInPixels(percentChar, kTextSpacingY);
	constexpr int32_t paddingBetween = 2;
	const int32_t totalWidth = valueWidth + percentCharWidth + paddingBetween;

	int32_t x = startX + (width - totalWidth) / 2 + 1;
	int32_t y = startY + 3;
	image.drawString(valueString.c_str(), x, y, kTextSpacingX, kTextSpacingY);

	x += valueWidth + paddingBetween;
	image.drawChar(percentChar, x, y, kTextSpacingX, kTextSpacingY);
}

void Number::drawKnob(int32_t startX, int32_t startY, int32_t width, int32_t height) {
	oled_canvas::Canvas& image = OLED::main;

	// Draw the background arc
	// Easier to adjust pixel-perfect, so we use a bitmap
	image.drawIconCentered(OLED::knobArcIcon, startX, width, startY);

	// Calculate current value angle
	constexpr int32_t knobRadius = 10;
	const int32_t centerX = startX + width / 2;
	const int32_t centerY = startY + knobRadius + 1;
	constexpr int32_t arcRangeAngle = 210, beginningAngle = 165;
	const float valuePercent = getNormalizedValue();
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
	image.drawLine(round(xStart), round(yStart), round(xEnd), round(yEnd), {.thick = true});

	// If the knob's position is near left or near right, fill the gap on the bitmap (the gap exists purely for
	// stylistic effect)
	if (currentAngle < 180.0f) {
		image.drawPixel(centerX - knobRadius, startY + height - 4);
	}
	if (currentAngle > 360.0f) {
		image.drawPixel(centerX + knobRadius, startY + height - 4);
	}
}

void Number::drawBar(int32_t startX, int32_t startY, int32_t slotWidth, int32_t slotHeight) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr uint8_t bar_width = 21;
	constexpr uint8_t bar_height = 7;
	constexpr uint8_t outline_padding = 2;
	const uint8_t bar_start_x = startX + 3 + outline_padding;
	const uint8_t bar_start_y = startY + 2 + outline_padding;
	const uint8_t bar_end_x = bar_start_x + bar_width - 1;
	const uint8_t bar_end_y = bar_start_y + bar_height - 1;

	// Draw outline
	image.drawRectangleRounded(bar_start_x - outline_padding, bar_start_y - outline_padding,
	                           bar_end_x + outline_padding, bar_end_y + outline_padding);

	// Draw bar fill
	const float value = getNormalizedValue();
	const uint8_t fill_width = value > 0.f ? std::ceil(value * bar_width) : 0;
	image.invertArea(bar_start_x, fill_width, bar_start_y, bar_end_y);

	// Draw indicators
	for (uint8_t x = bar_start_x + 1; x < bar_end_x; x += 3) {
		image.drawPixel(x, bar_start_y);
		image.drawPixel(x, bar_end_y);
	}

	// Middle accentuation
	const uint8_t middle_x = bar_start_x + bar_width / 2;
	if (value == 0.5f) {
		image.invertPixel(middle_x, bar_start_y - 1);
		image.invertPixel(middle_x, bar_end_y + 1);
		for (uint8_t y = bar_start_y; y <= bar_end_y; y += 2) {
			image.invertPixel(middle_x, y);
		}
	}
}

void Number::drawSlider(int32_t startX, int32_t startY, int32_t slotWidth, int32_t slotHeight) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr int32_t padding = 4;
	const int32_t minX = startX + padding;
	const int32_t maxX = startX + slotWidth - 1 - padding;
	const int32_t minY = startY + 2;
	const int32_t maxY = startY + slotHeight - 4;

	const float valueNormalized = getNormalizedValue();
	const int32_t valueLineMinX = minX;
	const int32_t valueLineWidth = static_cast<int32_t>(valueNormalized * (maxX - 1 - valueLineMinX));
	const int32_t valueLineX = valueLineMinX + valueLineWidth;

	const int32_t centerY = minY + (maxY - minY) / 2;
	for (int32_t x = minX; x <= maxX; x += 3) {
		if (x != valueLineX - 1 && x != valueLineX + 2) {
			image.drawPixel(x, centerY - 1);
			image.drawPixel(x, centerY + 1);
		}
	}

	image.drawVerticalLine(valueLineX, minY, maxY);
	image.drawVerticalLine(valueLineX + 1, minY, maxY);
}

void Number::drawLengthSlider(int32_t startX, int32_t startY, int32_t slotWidth, int32_t slotHeight,
                              bool minSliderPos) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr int32_t padding = 4;
	const int32_t minX = startX + padding;
	const int32_t maxX = startX + slotWidth - 1 - padding;
	const int32_t minY = startY + 2;
	const int32_t maxY = startY + slotHeight - 4;

	const float valueNormalized = getNormalizedValue();
	const int32_t valueLineMinX = minX + minSliderPos;
	const int32_t valueLineWidth = static_cast<int32_t>(valueNormalized * (maxX - valueLineMinX));
	const int32_t valueLineX = valueLineMinX + valueLineWidth;

	const int32_t centerY = minY + (maxY - minY) / 2;
	for (int32_t y = centerY - 1; y <= centerY + 1; y++) {
		image.drawHorizontalLine(y, minX, valueLineX);
	}
	image.drawVerticalLine(valueLineX, minY, maxY);

	for (int32_t x = maxX; x >= valueLineMinX; x -= 3) {
		image.drawPixel(x, centerY - 1);
		image.drawPixel(x, centerY + 1);
	}
}

void Number::getNotificationValue(StringBuf& value) {
	if (getNumberStyle() == PERCENT) {
		value.appendInt(getValue() * 2);
	}
	else {
		value.appendInt(getValue());
	}
}

} // namespace deluge::gui::menu_item
