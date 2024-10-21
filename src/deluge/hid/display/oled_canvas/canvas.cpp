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
#include "gui/fonts/fonts.h"
#include "storage/flash_storage.h"

using deluge::hid::display::oled_canvas::Canvas;

void Canvas::clearAreaExact(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY) {
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
	int32_t yRow = y >> 3;
	image_[yRow][x] |= 1 << (y & 0x7);
}

void Canvas::drawHorizontalLine(int32_t pixelY, int32_t startX, int32_t endX) {
	uint8_t mask = 1 << (pixelY & 7);

	uint8_t* __restrict__ currentPos = &image_[pixelY >> 3][startX];
	uint8_t* const lastPos = currentPos + (endX - startX);
	do {
		*currentPos |= mask;
		currentPos++;
	} while (currentPos <= lastPos);
}

void Canvas::drawVerticalLine(int32_t pixelX, int32_t startY, int32_t endY) {
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

void Canvas::drawRectangle(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY) {
	drawVerticalLine(minX, minY, maxY);
	drawVerticalLine(maxX, minY, maxY);

	drawHorizontalLine(minY, minX + 1, maxX - 1);
	drawHorizontalLine(maxY, minX + 1, maxX - 1);
}

void Canvas::drawString(std::string_view string, int32_t pixelX, int32_t pixelY, int32_t textWidth, int32_t textHeight,
                        int32_t scrollPos, int32_t endX, bool useTextWidth) {
	int32_t stringLength = string.length();
	int32_t charIdx = 0;
	int32_t charWidth = textWidth;
	// if the string is currently scrolling we want to identify the number of characters
	// that should be visible on the screen based on the current scroll position
	// to do iterate through each character in the string, based on its size in pixels
	// and compare that to the scroll position (which is also in pixels)
	// any characters before the scroll position are chopped off;
	if (scrollPos) {
		int32_t numCharsToChopOff = 0;
		int32_t widthOfCharsToChopOff = 0;
		int32_t charStartX = 0;
		for (char const c : string) {
			if (!useTextWidth) {
				int32_t charSpacing = getCharSpacingInPixels(c, textHeight, charIdx == stringLength);
				charWidth = getCharWidthInPixels(c, textHeight) + charSpacing;
			}
			charStartX += charWidth;
			// are we past the scroll position?
			// if so no more characters to chop off
			if (scrollPos < charStartX) {
				break;
			}
			// we haven't reached scroll position yet, so chop off these characters
			else {
				numCharsToChopOff++;
				widthOfCharsToChopOff += charWidth;
			}
			charIdx++;
		}

		// chop off the characters before the scroll position
		string = string.substr(numCharsToChopOff);
		// adjust scroll position to indicate how far we've scrolled
		scrollPos -= widthOfCharsToChopOff;
		// calculate new string length
		stringLength = string.length();
		// reset index
		charIdx = 0;
	}

	// if we scrolled above, then the string, ScrollPos, stringLength will have been adjusted
	// here we're going to draw the remaining characters in the string
	for (char const c : string) {
		if (!useTextWidth) {
			int32_t charSpacing = getCharSpacingInPixels(c, textHeight, charIdx == stringLength);
			charWidth = getCharWidthInPixels(c, textHeight) + charSpacing;
		}
		drawChar(c, pixelX, pixelY, charWidth, textHeight, scrollPos, endX);

		// calculate the X coordinate to draw the next character
		pixelX += (charWidth - scrollPos);

		// if we've reached the endX coordinate then we won't draw anymore characters
		if (pixelX >= endX) {
			break;
		}

		// no more scrolling
		scrollPos = 0;
		charIdx++;
	}
}

void Canvas::drawStringCentred(char const* string, int32_t pixelY, int32_t textWidth, int32_t textHeight,
                               int32_t centrePos) {
	std::string_view str{string};
	int32_t stringWidth = getStringWidthInPixels(string, textHeight);
	int32_t pixelX = centrePos - (stringWidth >> 1);
	drawString(str, pixelX, pixelY, textWidth, textHeight);
}

/// Draw a string, reducing its height so the string fits within the specified width
///
/// @param string A null-terminated C string
/// @param pixelY The Y coordinate of the top of the string
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
		else {
			newHeight = 7;
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
	if (theChar > '~') {
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
	// the smaller apple ][ is monospaced, so return standard width of each character
	else if (textHeight <= 9) {
		return kTextSpacingX;
	}

	lv_font_glyph_dsc_t const* descriptor;
	switch (textHeight) {
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
	bool monospacedFont = (textHeight <= 9);
	// don't add space to the last character
	if (isLastChar) {
		return 0;
	}
	else if (theChar == ' ') {
		// smaller apple ][ font is monospaced, so spacing is different
		if (monospacedFont) {
			return kTextSpacingX;
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
		// default spacing is 2 pixels for bold fonts
		else {
			return 2;
		}
	}
}

int32_t Canvas::getStringWidthInPixels(char const* string, int32_t textHeight) {
	std::string_view str{string};
	int32_t stringLength = str.length();
	int32_t stringWidth = 0;
	int32_t charIdx = 0;
	for (char const c : str) {
		int32_t charSpacing = getCharSpacingInPixels(c, textHeight, charIdx == stringLength);
		int32_t charWidth = getCharWidthInPixels(c, textHeight) + charSpacing;
		stringWidth += charWidth;
		charIdx++;
	}
	return stringWidth;
}

void Canvas::drawGraphicMultiLine(uint8_t const* graphic, int32_t startX, int32_t startY, int32_t width, int32_t height,
                                  int32_t numBytesTall) {
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

/// Draw a screen title and underline it.
///
/// @param text Title text
void Canvas::drawScreenTitle(std::string_view title) {
	int32_t extraY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 0 : 1;

	int32_t startY = extraY + OLED_MAIN_TOPMOST_PIXEL;

	drawString(title, 0, startY, kTextTitleSpacingX, kTextTitleSizeY);
	drawHorizontalLine(extraY + 11 + OLED_MAIN_TOPMOST_PIXEL, 0, OLED_MAIN_WIDTH_PIXELS - 1);
}

void Canvas::invertArea(int32_t xMin, int32_t width, int32_t startY, int32_t endY) {
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

/// inverts just the left edge
void Canvas::invertLeftEdgeForMenuHighlighting(int32_t xMin, int32_t width, int32_t startY, int32_t endY) {
	if (!FlashStorage::accessibilityMenuHighlighting) {
		return invertArea(xMin, width, startY, endY);
	}

	int32_t firstRowY = startY >> 3;
	int32_t lastRowY = endY >> 3;

	uint8_t currentRowMask = (255 << (startY & 7));
	uint8_t lastRowMask = (255 >> (7 - (endY & 7)));

	// For each row
	for (int32_t rowY = firstRowY; rowY <= lastRowY; rowY++) {

		if (rowY == lastRowY) {
			currentRowMask &= lastRowMask;
		}

		uint8_t* __restrict__ leftEdgePos = &image_[rowY][xMin];
		*leftEdgePos ^= currentRowMask;

		currentRowMask = 0xFF;
	}
}
