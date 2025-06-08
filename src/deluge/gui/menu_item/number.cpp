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

	const auto horizontalMenuStyle = runtimeFeatureSettings.get(HorizontalMenuStyle);
	const auto numberStyle = getNumberStyle();

	if (horizontalMenuStyle == Numeric || numberStyle == NUMBER) {
		DEF_STACK_STRING_BUF(paramValue, 10);
		paramValue.appendInt(getValue());
		return image.drawStringCentered(paramValue, startX, startY + 4, kTextTitleSpacingX, kTextTitleSizeY, width);
	}

	if (numberStyle == KNOB) {
		constexpr int32_t knobRadius = 10;
		const int32_t centerX = (startX + width / 2) - 2;
		const int32_t centerY = startY + knobRadius + 3;
		return drawKnob(knobRadius, centerX, centerY);
	}

	if (numberStyle == VERTICAL_BAR) {
		return drawVerticalBar(startX, startY, width, height);
	}
}

void Number::drawKnob(int32_t radius, int32_t centerX, int32_t centerY) {
	hid::display::oled_canvas::Canvas& image = hid::display::OLED::main;

	// Draw the background arc
	constexpr int32_t numSteps = 32;
	constexpr float angleStep = 180.0f / numSteps;

	for (int32_t i = 0; i <= numSteps; i++) {
		const float angle = 180.0f + (i * angleStep);
		const float radians = (angle * M_PI) / 180.0f;

		const int32_t x = centerX + static_cast<int32_t>(round(radius * cos(radians)));
		const int32_t y = centerY + static_cast<int32_t>(round(radius * sin(radians)));
		image.drawPixel(x, y);
	}

	// Calculate current value position
	const int32_t minValue = getMinValue();
	const int32_t maxValue = getMaxValue();
	const float valuePercent = static_cast<float>(getValue() - minValue) / static_cast<float>(maxValue - minValue);

	// Discretize the angle to help with aliasing
	constexpr int32_t numPositions = 15;
	const float discretePercent = round(valuePercent * (numPositions - 1)) / (numPositions - 1);

	// Draw the indicator line
	const float currentAngle = 180.0f + (180.0f * discretePercent);
	const float radians = (currentAngle * M_PI) / 180.0f;
	const int32_t startRadius = radius / 3;
	constexpr int32_t numStepsLine = 32;

	for (int32_t i = 0; i <= numStepsLine; i++) {
		const float r = startRadius + (static_cast<float>(i) * (radius - startRadius - 1) / numStepsLine);
		const int32_t x = centerX + static_cast<int32_t>(round(r * cos(radians)));
		const int32_t y = centerY + static_cast<int32_t>(round(r * sin(radians)));
		image.drawPixel(x, y);

		// Add thickness based on the angle
		if (abs(sin(radians)) > 0.707f) {
			image.drawPixel(x + 1, y);
		}
		else {
			image.drawPixel(x, y - 1);
		}
	}
}

void Number::drawVerticalBar(int32_t startX, int32_t startY, int32_t slotWidth, int32_t slotHeight) {
	hid::display::oled_canvas::Canvas& image = hid::display::OLED::main;

	constexpr int32_t barWidth = 18;

	constexpr int32_t dotsInterval = 3;
	constexpr int32_t topBottomPadding = 3;
	constexpr int32_t fillPadding = 3;
	const int32_t leftPadding = ((slotWidth - barWidth) / 2) - 2;

	const int32_t minX = startX + leftPadding;
	const int32_t maxX = minX + barWidth;
	const int32_t minY = startY + topBottomPadding;
	const int32_t maxY = minY + slotHeight - topBottomPadding - 2;
	const int32_t barHeight = maxY - minY;

	// Draw the dots at left and right sides
	// 1px offset from the bottom is better than 1px offset from the top
	for (int32_t y = minY; y < maxY; y += dotsInterval) {
		image.drawPixel(minX, y);
		image.drawPixel(maxX, y);
	}

	// Calculate fill height based on value
	const int32_t minValue = getMinValue();
	const int32_t maxValue = getMaxValue();
	const float valuePercent = static_cast<float>(getValue() - minValue) / static_cast<float>(maxValue - minValue);

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
