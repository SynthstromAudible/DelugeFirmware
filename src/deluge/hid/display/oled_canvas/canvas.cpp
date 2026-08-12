/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "canvas.h"
#include "definitions_cxx.hpp"
#include "deluge/util/d_string.h"
#include "gui/fonts/fonts.h"
#include "storage/flash_storage.h"

#include <algorithm>
#include <hid/display/oled.h>
#include <math.h>
#include <vector>

using deluge::hid::display::oled_canvas::Canvas;

namespace {
constexpr int32_t kMaxX = OLED_MAIN_WIDTH_PIXELS - 1;
constexpr int32_t kMaxY = OLED_MAIN_HEIGHT_PIXELS - 1;

bool clipRect(int32_t& minX, int32_t& minY, int32_t& maxX, int32_t& maxY) {
	if (maxX < minX || maxY < minY || maxX < 0 || minX > kMaxX || maxY < 0 || minY > kMaxY) {
		return false;
	}

	minX = std::clamp<int32_t>(minX, 0, kMaxX);
	maxX = std::clamp<int32_t>(maxX, 0, kMaxX);
	minY = std::clamp<int32_t>(minY, 0, kMaxY);
	maxY = std::clamp<int32_t>(maxY, 0, kMaxY);
	return true;
}
} // namespace

void Canvas::clearAreaExact(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY) {
	if (!clipRect(minX, minY, maxX, maxY)) {
		return;
	}

	int32_t firstRow = minY >> 3;
	int32_t lastRow = maxY >> 3;

	int32_t firstCompleteRow = firstRow;
	int32_t lastCompleteRow = lastRow;

	int32_t lastRowPixelWithin = maxY & 7;
	bool willDoLastRow = (lastRowPixelWithin != 7);
	uint8_t lastRowMask = 255 << (lastRowPixelWithin + 1);

	// First row
	int32_t firstRowPixelWithin = minY & 7;
	if (firstRowPixelWithin) {
		firstCompleteRow++;
		uint8_t firstRowMask = ~(255 << firstRowPixelWithin);
		if (willDoLastRow && firstRow == lastRow) {
			firstRowMask &= lastRowMask;
		}
		for (int32_t x = minX; x <= maxX; x++) {
			image_[firstRow][x] &= firstRowMask;
		}

		if (firstRow == lastRow) {
			return;
		}
	}

	// Last row
	if (willDoLastRow) {
		lastCompleteRow--;
		for (int32_t x = minX; x <= maxX; x++) {
			image_[lastRow][x] &= lastRowMask;
		}
	}

	for (int32_t row = firstCompleteRow; row <= lastCompleteRow; row++) {
		memset(&image_[row][minX], 0, maxX - minX + 1);
	}
}

void Canvas::drawPixel(int32_t x, int32_t y) {
	if (x < 0 || x > kMaxX || y < 0 || y > kMaxY) {
		return;
	}

	int32_t yRow = y >> 3;
	image_[yRow][x] |= 1 << (y & 0x7);
}

void Canvas::clearPixel(int32_t x, int32_t y) {
	if (x < 0 || x > kMaxX || y < 0 || y > kMaxY) {
		return;
	}

	int32_t yRow = y >> 3;
	image_[yRow][x] &= ~(1 << (y & 0x7));
}

void Canvas::invertPixel(int32_t x, int32_t y) {
	if (x < 0 || x > kMaxX || y < 0 || y > kMaxY) {
		return;
	}

	int32_t yRow = y >> 3;
	image_[yRow][x] ^= 1 << (y & 0x7);
}

void Canvas::drawHorizontalLine(int32_t pixelY, int32_t startX, int32_t endX) {
	if (pixelY < 0 || pixelY > kMaxY || endX < startX || endX < 0 || startX > kMaxX) {
		return;
	}

	startX = std::clamp<int32_t>(startX, 0, kMaxX);
	endX = std::clamp<int32_t>(endX, 0, kMaxX);
	if (endX < startX) {
		return;
	}

	uint8_t mask = 1 << (pixelY & 7);

	uint8_t* __restrict__ currentPos = &image_[pixelY >> 3][startX];
	uint8_t* const lastPos = currentPos + (endX - startX);
	do {
		*currentPos |= mask;
		currentPos++;
	} while (currentPos <= lastPos);
}

void Canvas::drawVerticalLine(int32_t pixelX, int32_t startY, int32_t endY) {
	if (pixelX < 0 || pixelX > kMaxX || endY < startY || endY < 0 || startY > kMaxY) {
		return;
	}

	startY = std::clamp<int32_t>(startY, 0, kMaxY);
	endY = std::clamp<int32_t>(endY, 0, kMaxY);
	if (endY < startY) {
		return;
	}

	int32_t firstRowY = startY >> 3;
	int32_t lastRowY = endY >> 3;

	uint8_t firstRowMask = (255 << (startY & 7));
	uint8_t lastRowMask = (255 >> (7 - (endY & 7)));

	uint8_t* __restrict__ currentPos = &image_[firstRowY][pixelX];

	if (firstRowY == lastRowY) {
		uint8_t mask = firstRowMask & lastRowMask;
		*currentPos |= mask;
	}

	else {
		uint8_t* const finalPos = &image_[lastRowY][pixelX];

		// First row
		*currentPos |= firstRowMask;

		// Intermediate rows
		goto drawSolid;
		do {
			*currentPos = 255;
drawSolid:
			currentPos += OLED_MAIN_WIDTH_PIXELS;
		} while (currentPos != finalPos);

		// Last row
		*currentPos |= lastRowMask;
	}
}

void Canvas::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const DrawLineOptions& options) {
	const bool steep = abs(y1 - y0) > abs(x1 - x0);
	if (steep) {
		std::swap(x0, y0);
		std::swap(x1, y1);
	}
	if (x0 > x1) {
		std::swap(x0, x1);
		std::swap(y0, y1);
	}

	int32_t dx = x1 - x0;
	int32_t dy = abs(y1 - y0);
	int32_t error = dx / 2;
	int32_t y = y0;
	int32_t y_step = y0 < y1 ? 1 : -1;

	for (int32_t x = x0; x <= x1; x++) {
		int32_t actual_x = steep ? y : x;
		int32_t actual_y = steep ? x : y;

		if (options.max_x.has_value() && actual_x > options.max_x.value()) {
			return;
		}
		if (!options.min_x.has_value() || actual_x >= options.min_x.value()) {
			drawPixel(actual_x, actual_y);

			if (options.thick) {
				drawPixel(steep ? actual_x + 1 : actual_x, steep ? actual_y : actual_y - 1);
			}

			if (options.point_callback.has_value()) {
				options.point_callback.value()({actual_x, actual_y});
			}
		}

		error -= dy;
		if (error < 0) {
			y += y_step;
			error += dx;
		}
	}
}

void Canvas::drawRectangle(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY) {
	drawVerticalLine(minX, minY, maxY);
	drawVerticalLine(maxX, minY, maxY);

	drawHorizontalLine(minY, minX + 1, maxX - 1);
	drawHorizontalLine(maxY, minX + 1, maxX - 1);
}

void Canvas::drawRectangleRounded(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY, BorderRadius radius) {
	if (!roundedCornersEnabled) {
		drawRectangle(minX, minY, maxX, maxY);
		return;
	}

	const int32_t radiusPixels = radius == SMALL ? 1 : 2;

	drawVerticalLine(minX, minY + radiusPixels, maxY - radiusPixels);
	drawVerticalLine(maxX, minY + radiusPixels, maxY - radiusPixels);
	drawHorizontalLine(minY, minX + radiusPixels, maxX - radiusPixels);
	drawHorizontalLine(maxY, minX + radiusPixels, maxX - radiusPixels);

	if (radiusPixels == 2) {
		drawPixel(minX + 1, minY + 1); //< Top-left corner
		drawPixel(maxX - 1, minY + 1); //< Top-right corner
		drawPixel(minX + 1, maxY - 1); //< Bottom-left corner
		drawPixel(maxX - 1, maxY - 1); //< Bottom-right corner
	}
}

void Canvas::drawCircle(int32_t centerX, int32_t centerY, int32_t radius, bool fill) {
	int32_t x = 0;
	int32_t y = radius;
	// Add a small bias for small radii — helps round out edges
	int32_t d = 1 - radius + (radius <= 6 ? 1 : 0);

	auto plot_circle_points = [&](int32_t cx, int32_t cy, int32_t x, int32_t y) {
		if (fill) {
			// Fill horizontally between symmetric points
			drawHorizontalLine(cy + y, cx - x, cx + x);
			drawHorizontalLine(cy - y, cx - x, cx + x);
			drawHorizontalLine(cy + x, cx - y, cx + y);
			drawHorizontalLine(cy - x, cx - y, cx + y);
		}
		else {
			// Just outline
			drawPixel(cx + x, cy + y);
			drawPixel(cx - x, cy + y);
			drawPixel(cx + x, cy - y);
			drawPixel(cx - x, cy - y);
			drawPixel(cx + y, cy + x);
			drawPixel(cx - y, cy + x);
			drawPixel(cx + y, cy - x);
			drawPixel(cx - y, cy - x);
		}
	};

	while (x <= y) {
		plot_circle_points(centerX, centerY, x, y);

		// normal midpoint update
		if (d < 0) {
			d += 2 * x + 3;
		}
		else {
			d += 2 * (x - y) + 5;
			y--;
		}
		x++;

		// Small tweak: for very small circles, gradually adjust d to round diagonals
		if (radius <= 5) {
			d += x % 2 == 0 ? 1 : 0;
		}
	}
}

void Canvas::drawString(std::string_view string, int32_t pixelX, int32_t pixelY, int32_t textWidth, int32_t textHeight,
                        int32_t scrollPos, int32_t endX, bool useTextWidth) {
	int32_t lastIndex = static_cast<int32_t>(string.length()) - 1;
	int32_t advanceWidth = textWidth;
	int32_t drawWidth = textWidth;
	// if the string is currently scrolling we want to identify the number of characters
	// that should be visible on the screen based on the current scroll position
	// to do iterate through each character in the string, based on its size in pixels
	// and compare that to the scroll position (which is also in pixels)
	// any characters before the scroll position are chopped off;
	if (scrollPos) {
		int32_t numCharsToChopOff = 0;
		int32_t widthOfCharsToChopOff = 0;
		int32_t charStartX = 0;
		for (int32_t i = 0; i < static_cast<int32_t>(string.size()); ++i) {
			char const current_char = string[i];
			if (!useTextWidth) {
				int32_t charSpacing = getCharSpacingInPixels(current_char, textHeight, i == lastIndex);
				// calculate the width of the current character in pixels, including any spacing adjustments
				advanceWidth = getCharWidthInPixels(current_char, textHeight) + charSpacing;
			}

			// if we're not on the first character
			if (i > 0) {
				// get the previous character in the string
				char const previous_char = string[i - 1];
				// calculate the starting X position for the current character, taking into account any spacing
				// adjustments based on the previous character
				charStartX += getPreviousCharSpacingAdjustmentInPixels(previous_char, current_char, textHeight);
			}

			// calculate the X coordinate to draw the next character
			charStartX += advanceWidth;

			// are we past the scroll position?
			// if so no more characters to chop off
			if (scrollPos < charStartX) {
				break;
			}
			// we haven't reached scroll position yet, so chop off these characters
			else {
				numCharsToChopOff++;
				// we need to keep track of the width of the characters that are being chopped off, so we can adjust the
				// scroll position accordingly
				widthOfCharsToChopOff += advanceWidth;
			}
		}

		// chop off the characters before the scroll position
		string = string.substr(numCharsToChopOff);
		// adjust scroll position to indicate how far we've scrolled
		scrollPos -= widthOfCharsToChopOff;
		// update the last index to reflect the new string length
		lastIndex = static_cast<int32_t>(string.length()) - 1;
	}

	// if we scrolled above, then the string and scroll position will have been adjusted
	// here we're going to draw the remaining characters in the string
	for (int32_t i = 0; i < static_cast<int32_t>(string.size()); ++i) {
		char const current_char = string[i];
		if (!useTextWidth) {
			const int32_t charWidth = getCharWidthInPixels(current_char, textHeight);
			// drawChar centres the glyph inside this width. Keep that draw box independent of final-character
			// spacing, so the same glyph pixels do not shift when a word is drawn alone vs. as a prefix.
			const int32_t advanceSpacing = getCharSpacingInPixels(current_char, textHeight, i == lastIndex);
			const int32_t drawSpacing = getCharSpacingInPixels(current_char, textHeight, false);
			advanceWidth = charWidth + advanceSpacing;
			drawWidth = charWidth + drawSpacing;
		}

		// if we're not on the first character
		if (i > 0) {
			// get the previous character in the string
			char const previous_char = string[i - 1];
			// calculate the starting X position for the current character, taking into account any spacing adjustments
			// based on the previous character
			pixelX += getPreviousCharSpacingAdjustmentInPixels(previous_char, current_char, textHeight);
		}

		drawChar(current_char, pixelX, pixelY, drawWidth, textHeight, scrollPos, endX);

		// calculate the X coordinate to draw the next character
		pixelX += (advanceWidth - scrollPos);

		// if we've reached the endX coordinate then we won't draw anymore characters
		if (pixelX >= endX) {
			break;
		}

		// no more scrolling
		scrollPos = 0;
	}
}

void Canvas::drawStringCentred(char const* string, int32_t pixelY, int32_t textWidth, int32_t textHeight,
                               int32_t centrePos) {
	std::string_view str{string};
	int32_t stringWidth = getStringWidthInPixels(string, textHeight);
	int32_t pixelX = centrePos - (stringWidth >> 1);
	drawString(str, pixelX, pixelY, textWidth, textHeight);
}

void Canvas::drawStringCentered(char const* string, int32_t pixelX, int32_t pixelY, int32_t textSpacingX,
                                int32_t textSpacingY, int32_t totalWidth) {
	DEF_STACK_STRING_BUF(stringBuf, 24);
	stringBuf.append(string);
	drawStringCentered(stringBuf, pixelX, pixelY, textSpacingX, textSpacingY, totalWidth);
}

void Canvas::drawStringCentered(StringBuf& stringBuf, int32_t pixelX, int32_t pixelY, int32_t textSpacingX,
                                int32_t textSpacingY, int32_t totalWidth) {
	int32_t stringWidth;

	// Trim characters from the end until it fits.
	while ((stringWidth = getStringWidthInPixels(stringBuf.c_str(), textSpacingY)) >= totalWidth - 3) {
		stringBuf.truncate(stringBuf.size() - 1);
	}

	// Padding to center the string
	const float padding = (totalWidth - stringWidth) / 2.0f;
	int32_t paddingAsInt = static_cast<int32_t>(padding);

	if (padding != paddingAsInt) {
		// If we can't center exactly, 1px right is better than 1px left.
		paddingAsInt++;
	}

	drawString(stringBuf.c_str(), pixelX + paddingAsInt, pixelY, stringWidth, textSpacingY);
}

/// Draw a string, reducing its height so the string fits within the specified width
///
/// @param string A null-terminated C string
/// @param textWidth Requested width for each character in the string
/// @param textHeight Requested height for each character in the string
void Canvas::drawStringCentredShrinkIfNecessary(char const* string, int32_t pixelY, int32_t textWidth,
                                                int32_t textHeight) {
	bool shrink = false;
	std::string_view str{string};
	int32_t maxTextWidth = (uint8_t)OLED_MAIN_WIDTH_PIXELS / (uint32_t)str.length();
	if (textWidth > maxTextWidth) {
		int32_t newHeight = (uint32_t)(textHeight * maxTextWidth) / (uint32_t)textWidth;
		if (newHeight >= 20) {
			newHeight = 20;
		}
		else if (newHeight >= 13) {
			newHeight = 13;
		}
		else if (newHeight >= 10) {
			newHeight = 10;
		}
		else if (newHeight >= 7) {
			newHeight = 7;
		}
		else {
			newHeight = 5;
		}

		textWidth = maxTextWidth;

		int32_t heightDiff = textHeight - newHeight;
		pixelY += heightDiff >> 1;
		textHeight = newHeight;

		shrink = true;
	}
	int32_t pixelX = (kImageWidth - textWidth * str.length()) >> 1;
	drawString(str, pixelX, pixelY, textWidth, textHeight, 0, OLED_MAIN_WIDTH_PIXELS, shrink);
}

void Canvas::drawStringAlignRight(char const* string, int32_t pixelY, int32_t textWidth, int32_t textHeight,
                                  int32_t rightPos) {
	std::string_view str{string};
	int32_t stringWidth = getStringWidthInPixels(string, textHeight);
	int32_t pixelX = rightPos - (stringWidth);
	drawString(str, pixelX, pixelY, textWidth, textHeight);
}

#define DO_CHARACTER_SCALING 0

void Canvas::drawChar(uint8_t theChar, int32_t pixelX, int32_t pixelY, int32_t spacingX, int32_t textHeight,
                      int32_t scrollPos, int32_t endX) {
	int32_t charIndex = getCharIndex(theChar);
	if (charIndex <= 0) {
		return;
	}

	lv_font_glyph_dsc_t const* descriptor;
	uint8_t const* font;
	int32_t fontNativeHeight;

	switch (textHeight) {
	case 5:
		[[fallthrough]];
	case 6:
		textHeight = 5;
		descriptor = font_5px_desc;
		font = font_5px;
		fontNativeHeight = 5;
		break;
	case 9:
		pixelY++;
		[[fallthrough]];
	case 7:
		[[fallthrough]];
	case 8:
		textHeight = 7;
		descriptor = font_apple_desc;
		font = font_apple;
		fontNativeHeight = 8;
		break;
	case 10:
		textHeight = 9;
		descriptor = font_metric_bold_9px_desc;
		font = font_metric_bold_9px;
		fontNativeHeight = 9;
		break;
	case 13:
		descriptor = font_metric_bold_13px_desc;
		font = font_metric_bold_13px;
		fontNativeHeight = 13;
		break;
	case 20:
		[[fallthrough]];
	default:
		fontNativeHeight = 20;
		descriptor = font_metric_bold_20px_desc;
		font = font_metric_bold_20px;
		break;
	}

	descriptor += charIndex;

#if DO_CHARACTER_SCALING
	int32_t scaledFontWidth =
	    (uint16_t)(descriptor->w_px * textHeight + (fontNativeHeight >> 1) - 1) / (uint8_t)fontNativeHeight;
#else
	int32_t scaledFontWidth = descriptor->w_px;
#endif
	pixelX += (spacingX - scaledFontWidth) >> 1;

	if (pixelX < 0) {
		scrollPos += -pixelX;
		pixelX = 0;
	}

	int32_t bytesPerCol = ((textHeight - 1) >> 3) + 1;

	int32_t textWidth = descriptor->w_px - scrollPos;
	drawGraphicMultiLine(&font[descriptor->glyph_index + scrollPos * bytesPerCol], pixelX, pixelY, textWidth,
	                     textHeight, bytesPerCol);
}

int32_t Canvas::getCharIndex(uint8_t theChar) {
	// 129 represents flat glyph
	if (theChar == 129) {
		theChar = '~' + 1;
	}
	else if (theChar > '~') {
		return 0;
	}

	if (theChar >= 'a') {
		if (theChar <= 'z') {
			theChar -= 32;
		}
		else {
			theChar -= 26; // Lowercase chars have been snipped out of the tables.
		}
	}

	int32_t charIndex = theChar - 0x20;
	return charIndex;
}

int32_t Canvas::getCharWidthInPixels(uint8_t theChar, int32_t textHeight) {
	int32_t charIndex = getCharIndex(theChar);
	if (charIndex <= 0) {
		return 0;
	}
	else if (textHeight >= 7 && textHeight <= 9) {
		// the smaller apple ][ is monospaced, so return standard width of each character
		return kTextSpacingX;
	}

	lv_font_glyph_dsc_t const* descriptor;
	switch (textHeight) {
	case 5:
		descriptor = font_5px_desc;
		break;
	case 10:
		descriptor = font_metric_bold_9px_desc;
		break;
	case 13:
		descriptor = font_metric_bold_13px_desc;
		break;
	case 20:
		[[fallthrough]];
	default:
		descriptor = font_metric_bold_20px_desc;
		break;
	}

	descriptor += charIndex;
	return descriptor->w_px;
}

int32_t Canvas::getCharSpacingInPixels(uint8_t theChar, int32_t textHeight, bool isLastChar) {
	bool monospacedFont = (textHeight >= 7 and textHeight <= 9);
	// don't add space to the last character
	if (isLastChar) {
		return 0;
	}
	else if (theChar == ' ') {
		// smaller apple ][ font is monospaced, so spacing is different
		if (monospacedFont) {
			return kTextSpacingX;
		}
		// small font is spaced 2px
		else if (textHeight <= 6) {
			return 2;
		}
		// if character is a space, make spacing 6px instead
		// (just need to add 5 since previous character added 1 after it)
		else {
			return 5;
		}
	}
	else {
		// smaller apple ][ font is monospaced, so no extra spacing needs to be added
		// as it's handled by the standard char width
		if (monospacedFont) {
			return 0;
		}
		// small font
		else if (textHeight <= 6) {
			return 1;
		}
		// default spacing is 2 pixels for bold fonts
		else {
			return 2;
		}
	}
}

namespace {
struct KerningRule {
	uint8_t textHeight;
	uint8_t previousChar;
	uint8_t currentChar;
	int8_t adjustment;
};

// Store letter kerning rules once using uppercase chars; lowercase input should use the same spacing.
constexpr uint8_t normaliseKerningChar(uint8_t theChar) {
	if (theChar >= 'a' && theChar <= 'z') {
		return static_cast<uint8_t>(theChar - ('a' - 'A'));
	}
	return theChar;
}

// Kerning rules for specific character pairs at different text heights
// Format: {textHeight, previousChar, currentChar, adjustment}
constexpr KerningRule kKerningRules[] = {
    // Title/menu font (textHeight 10): exact pairs
    {10, 'P', 'A', -1},
    {10, 'W', 'A', -2},
    {10, 'Y', 'A', -2},
    {10, '7', 'J', -1},
    {10, 'A', 'O', -1},
    {10, 'W', 'O', -1},
    {10, 'Y', 'O', -1},
    {10, 'N', 'R', 1},
    {10, 'Y', 'S', -1},
    {10, 'A', 'T', -1},
    {10, 'L', 'T', -1},
    {10, '\'', 'T', -1},
    {10, '"', 'T', -1},
    {10, '4', 'T', -1},
    {10, '6', 'T', -1},
    {10, 'A', 'V', -1},
    {10, 'A', 'W', -4},
    {10, 'D', 'W', -1},
    {10, 'A', 'Y', -2},
    {10, 'W', '.', -2},
    {10, 'F', '.', -2},
    {10, 'A', '"', -1},
    {10, 'L', '"', -2},
    {10, 'V', '/', -1},
    {10, '7', '4', -1},

    // 13px font: exact pairs.
    {13, 'B', 'A', -1},
    {13, 'W', 'A', -4},
    {13, '-', 'N', 2},
    {13, 'E', 'S', 1},
    {13, 'L', 'T', -1},
    {13, '4', 'T', -1},
    {13, '6', 'T', -1},
    {13, '1', '4', -1},
    {13, '6', '4', -1},
    {13, '7', '4', -2},
    {13, '7', '6', -1},
    {13, '7', '8', -1},
    {13, '2', '9', -1},
    {13, '7', '9', -1},

    // 20px font: exact pairs.
    {20, '2', '1', -1},
    {20, '2', '7', -1},
};

} // namespace

int32_t Canvas::getPreviousCharSpacingAdjustmentInPixels(uint8_t previousChar, uint8_t currentChar,
                                                         int32_t textHeight) {
	// don't adjust spacing around a space character
	if (previousChar == ' ' || currentChar == ' ') {
		return 0;
	}

	// normalise the characters to uppercase for kerning rules, since lowercase letters use the same spacing
	// e.g. a becomes A
	// if in the future we treat lowercase letters differently,
	// we can remove this normalisation and add specific kerning rules for lowercase letters
	previousChar = normaliseKerningChar(previousChar);
	currentChar = normaliseKerningChar(currentChar);

	// loop through all configured kerning adjustment rules
	for (const KerningRule& rule : kKerningRules) {
		// check for a kerning rule that matches the previous and current characters at the given text height
		if (rule.textHeight == textHeight && rule.previousChar == previousChar && rule.currentChar == currentChar) {
			// rule found, return kerning adjustment
			return rule.adjustment;
		}
	}

	// if no matching kerning rule is found, return 0 (no adjustment)
	return 0;
}

// Calculate the width of a string in pixels, taking into account character widths, spacing, and kerning adjustments
int32_t Canvas::getStringWidthInPixels(char const* string, int32_t textHeight) {
	std::string_view str{string};
	// Get the index of the last character in the string
	int32_t lastIndex = static_cast<int32_t>(str.length()) - 1;
	// Initialize variables to track the total advance width and the maximum glyph end position
	int32_t advanceX = 0;
	int32_t maxGlyphEndX = 0;

	// Loop through each character in the string
	for (int32_t i = 0; i < static_cast<int32_t>(str.size()); ++i) {
		// Get the current character
		char const current_char = str[i];
		// Calculate the width of the current character in pixels
		const int32_t charWidth = getCharWidthInPixels(current_char, textHeight);
		// Calculate the spacing for the current character based on whether it's the last character in the string
		const int32_t advanceSpacing = getCharSpacingInPixels(current_char, textHeight, i == lastIndex);
		// Calculate the spacing for drawing the current character (not considering last character)
		const int32_t drawSpacing = getCharSpacingInPixels(current_char, textHeight, false);
		// Calculate the total width for drawing and advancing the cursor for the current character
		const int32_t drawWidth = charWidth + drawSpacing;
		// Calculate the total width for advancing the cursor after drawing the current character
		const int32_t advanceWidth = charWidth + advanceSpacing;

		// if we're not on the first character
		if (i > 0) {
			// Get the previous character in the string
			char const previous_char = str[i - 1];
			// Adjust the advanceX position based on any kerning adjustments between the previous and current characters
			advanceX += getPreviousCharSpacingAdjustmentInPixels(previous_char, current_char, textHeight);
		}

		// Match drawString(): final-character spacing affects cursor advance, while glyph extents are measured
		// from the stable draw box used when the bitmap is centred.
		if (charWidth > 0) {
			// Calculate the starting X position for the glyph, centering it within the draw width
			const int32_t glyphStartX = advanceX + ((drawWidth - charWidth) >> 1);
			// Update the maximum glyph end position based on the current glyph's end position
			maxGlyphEndX = std::max(maxGlyphEndX, glyphStartX + charWidth);
		}

		// Update the advanceX position for the next character
		advanceX += advanceWidth;
	}
	// Return the maximum of the total advance width and the maximum glyph end position to ensure the string width
	// accounts for both cursor advancement and glyph size
	return std::max(advanceX, maxGlyphEndX);
}

void Canvas::drawGraphicMultiLine(uint8_t const* graphic, int32_t startX, int32_t startY, int32_t width, int32_t height,
                                  int32_t numBytesTall, bool reversed) {
	if (reversed) {
		std::vector<uint8_t> reversedGraphic(width * numBytesTall);
		for (int32_t col = 0; col < width; ++col) {
			int32_t inputIndex = col * numBytesTall;
			int32_t reversedCol = width - 1 - col;
			int32_t outputIndex = reversedCol * numBytesTall;
			for (int32_t byte = 0; byte < numBytesTall; ++byte) {
				reversedGraphic[outputIndex + byte] = graphic[inputIndex + byte];
			}
		}
		return drawGraphicMultiLine(reversedGraphic.data(), startX, startY, width, height, numBytesTall);
	}

	int32_t rowOnDisplay = startY >> 3;
	int32_t yOffset = startY & 7;
	int32_t rowOnGraphic = 0;

	if (width > OLED_MAIN_WIDTH_PIXELS - startX) {
		width = OLED_MAIN_WIDTH_PIXELS - startX;
	}

	// First row
	uint8_t* __restrict__ currentPos = &image_[rowOnDisplay][startX];
	uint8_t const* endPos = currentPos + width;
	uint8_t const* __restrict__ graphicPos = graphic;

	while (currentPos < endPos) {
		*currentPos |= (*graphicPos << yOffset);
		currentPos++;
		graphicPos += numBytesTall;
	}

	int32_t yOffsetNegative = 8 - yOffset;

	// Do middle rows
	while (true) {
		rowOnDisplay++;
		if (rowOnDisplay >= (OLED_MAIN_HEIGHT_PIXELS >> 3)) {
			return;
		}

		rowOnGraphic++;
		if (height <= ((rowOnGraphic << 3) - yOffset)) {
			return; // If no more of graphic to draw...
		}

		currentPos = &image_[rowOnDisplay][startX];
		endPos = currentPos + width;
		graphicPos = graphic++; // Takes value before we increment

		if (rowOnGraphic >= numBytesTall) {
			break; // If only drawing that remains is final row of graphic...
		}

		while (currentPos < endPos) {
			// Cleverly read 2 bytes in one go. Doesn't really speed things up. I should try addressing display
			// vertically, so I can do 32 bits in one go on both the graphic and the display...
			uint32_t data = *(uint16_t*)graphicPos;
			*currentPos |= data >> yOffsetNegative;

			//*currentPos |= (*graphicPos >> yOffsetNegative) | (*(graphicPos + 1) << yOffset);
			currentPos++;
			graphicPos += numBytesTall;
		}
	}

	// Final row
	while (currentPos < endPos) {
		*currentPos |= (*graphicPos >> yOffsetNegative);
		currentPos++;
		graphicPos += numBytesTall;
	}
}

void Canvas::drawIcon(const Icon& icon, int32_t x, int32_t y, bool reversed) {
	drawGraphicMultiLine(icon.data, x, y, icon.width, icon.height, icon.numBytesTall, reversed);
}
void Canvas::drawIconCentered(const Icon& icon, int32_t startX, int32_t totalWidth, int32_t y, bool reversed) {
	int32_t padding = (totalWidth - icon.width) >> 1;
	drawIcon(icon, startX + padding, y, reversed);
}

/// Draw a screen title and underline it.
///
/// @param text Title text
void Canvas::drawScreenTitle(std::string_view title, bool drawSeparator) {
	constexpr int32_t extraY = 1;
	constexpr int32_t startY = extraY + OLED_MAIN_TOPMOST_PIXEL;

	drawString(title, 0, startY, kTextTitleSpacingX, kTextTitleSizeY);

	if (drawSeparator) {
		drawHorizontalLine(kScreenTitleSeparatorY, 0, OLED_MAIN_WIDTH_PIXELS - 1);
	}
}

void Canvas::invertArea(int32_t xMin, int32_t width, int32_t startY, int32_t endY) {
	int32_t xMax = xMin + width - 1;
	if (!clipRect(xMin, startY, xMax, endY)) {
		return;
	}
	width = xMax - xMin + 1;

	int32_t firstRowY = startY >> 3;
	int32_t lastRowY = endY >> 3;

	uint8_t currentRowMask = (255 << (startY & 7));
	uint8_t lastRowMask = (255 >> (7 - (endY & 7)));

	// For each row
	for (int32_t rowY = firstRowY; rowY <= lastRowY; rowY++) {

		if (rowY == lastRowY) {
			currentRowMask &= lastRowMask;
		}

		uint8_t* __restrict__ currentPos = &image_[rowY][xMin];
		uint8_t* const endPos = currentPos + width;

		while (currentPos < endPos) {
			*currentPos ^= currentRowMask;
			currentPos++;
		}

		currentRowMask = 0xFF;
	}
}

void Canvas::invertAreaRounded(int32_t xMin, int32_t width, int32_t startY, int32_t endY, BorderRadius radius) {
	invertArea(xMin, width, startY, endY);

	if (!roundedCornersEnabled) {
		return;
	}

	const int32_t radiusPixels = radius == SMALL ? 1 : 2;

	// restore corners back
	const int32_t xMax = xMin + width - 1;

	if (radiusPixels == 1) {
		// For 1px radius, clear just the corner pixels
		clearPixel(xMin, startY); // Top-left corner
		clearPixel(xMax, startY); // Top-right corner
		clearPixel(xMin, endY);   // Bottom-left corner
		clearPixel(xMax, endY);   // Bottom-right corner
	}
	else if (radiusPixels == 2) {
		// For 2px radius, clear 3 pixels per corner
		// Top-left corner
		clearPixel(xMin, startY);
		clearPixel(xMin + 1, startY);
		clearPixel(xMin, startY + 1);

		// Top-right corner
		clearPixel(xMax, startY);
		clearPixel(xMax - 1, startY);
		clearPixel(xMax, startY + 1);

		// Bottom-left corner
		clearPixel(xMin, endY);
		clearPixel(xMin + 1, endY);
		clearPixel(xMin, endY - 1);

		// Bottom-right corner
		clearPixel(xMax, endY);
		clearPixel(xMax - 1, endY);
		clearPixel(xMax, endY - 1);
	}
}

/// inverts just the left edge
void Canvas::invertLeftEdgeForMenuHighlighting(int32_t xMin, int32_t width, int32_t startY, int32_t endY) {
	if (FlashStorage::accessibilityMenuHighlighting == MenuHighlighting::NO_INVERSION) {
		drawVerticalLine(xMin, startY, endY);
	}
	else {
		invertAreaRounded(xMin, width, startY, endY);
	}
}
