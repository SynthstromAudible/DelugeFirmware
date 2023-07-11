/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include "definitions.h"

#if HAVE_OLED

#include "hid/display/oled.h"
#include <string.h>
#include "gui/ui_timer_manager.h"
#include "processing/engines/audio_engine.h"
#include "util/d_string.h"
#include "RZA1/mtu/mtu.h"
#include "drivers/dmac/dmac.h"

extern "C" {
#include "RZA1/oled/oled_low_level.h"
#include "RZA1/rspi/rspi.h"
#include "drivers/oled/oled.h"
#include "gui/fonts/fonts.h"
#include "RZA1/uart/sio_char.h"
#include "util/cfunctions.h"
extern void v7_dma_flush_range(uint32_t start, uint32_t end);
}

extern uint8_t usbInitializationPeriodComplete;

namespace OLED {

uint8_t oledMainImage[OLED_MAIN_HEIGHT_PIXELS >> 3][OLED_MAIN_WIDTH_PIXELS];
uint8_t oledMainConsoleImage[CONSOLE_IMAGE_NUM_ROWS][OLED_MAIN_WIDTH_PIXELS];
uint8_t oledMainPopupImage[OLED_MAIN_HEIGHT_PIXELS >> 3][OLED_MAIN_WIDTH_PIXELS];

uint8_t (*oledCurrentImage)[OLED_MAIN_WIDTH_PIXELS] = oledMainImage;

int workingAnimationCount;
char const* workingAnimationText; // NULL means animation not active

int sideScrollerDirection; // 0 means none active

const uint8_t folderIcon[] = {
    0b11111100, 0b10000100, 0b10000110, 0b10000101, 0b10000011, 0b10000001, 0b10000001, 0b11111110,
};

const uint8_t waveIcon[] = {
    0b00010000, 0b11111110, 0b00111000, 0b00010000, 0b00111000, 0b01111100, 0b00111000, 0b00010000,
};

const uint8_t songIcon[] = {
    0, 0b01100000, 0b11110000, 0b11110000, 0b01111110, 0b00000110, 0b00000110, 0b00000011, 0b00000011,
};

const uint8_t synthIcon[] = {0b11111110, 0b11100000, 0b00000000, 0b11111110, 0b00000000, 0b11100000, 0b11111110, 0};

const uint8_t kitIcon[] = {
    0b00111100, 0b01001010, 0b11110001, 0b10010001, 0b10010001, 0b11110001, 0b01001010, 0b00111100,
};

const uint8_t downArrowIcon[] = {
    0b00010000, 0b00100000, 0b01111111, 0b00100000, 0b00010000,
};

const uint8_t rightArrowIcon[] = {
    0b00010101,
    0b00001110,
    0b00000100,
};

#if ENABLE_TEXT_OUTPUT
uint16_t renderStartTime;
#endif

void clearMainImage() {
#if ENABLE_TEXT_OUTPUT
	renderStartTime = *TCNT[TIMER_SYSTEM_FAST];
#endif

	stopBlink();
	stopScrollingAnimation();
	memset(oledMainImage, 0,
	       sizeof(oledMainImage)); // Takes about 1 fast-timer tick (whereas entire rendering takes around 8 to 15).
	                               // So, not worth trying to use DMA here or anything.
}

// Clears area *inclusive* of maxX, but not maxY? Stupid.
void clearAreaExact(int minX, int minY, int maxX, int maxY, uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	int firstRow = minY >> 3;
	int lastRow = maxY >> 3;

	int firstCompleteRow = firstRow;
	int lastCompleteRow = lastRow;

	int lastRowPixelWithin = maxY & 7;
	bool willDoLastRow = (lastRowPixelWithin != 7);
	uint8_t lastRowMask = 255 << (lastRowPixelWithin + 1);

	// First row
	int firstRowPixelWithin = minY & 7;
	if (firstRowPixelWithin) {
		firstCompleteRow++;
		uint8_t firstRowMask = ~(255 << firstRowPixelWithin);
		if (willDoLastRow && firstRow == lastRow) {
			firstRowMask &= lastRowMask;
		}
		for (int x = minX; x <= maxX; x++) {
			image[firstRow][x] &= firstRowMask;
		}

		if (firstRow == lastRow) {
			return;
		}
	}

	// Last row
	if (willDoLastRow) {
		lastCompleteRow--;
		for (int x = minX; x <= maxX; x++) {
			image[lastRow][x] &= lastRowMask;
		}
	}

	for (int row = firstCompleteRow; row <= lastCompleteRow; row++) {
		memset(&image[row][minX], 0, maxX - minX + 1);
	}
}

void moveAreaUpCrude(int minX, int minY, int maxX, int maxY, int delta, uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {

	int firstRow = minY >> 3;
	int lastRow = maxY >> 3;

	// First move any entire rows
	int deltaRows = delta >> 3;
	if (deltaRows) {
		delta &= 7;
		minY += delta; // There's a bit we can ignore here, potentially
		int firstRowHere = minY >> 3;
		lastRow -= deltaRows;
		for (int row = firstRow; row <= lastRow; row++) {
			memcpy(&image[row][minX], &image[row + deltaRows][minX], maxX - minX + 1);
		}
	}

	// Move final sub-row amount
	if (delta) {
		for (int x = minX; x <= maxX; x++) {
			uint8_t carry;

			for (int row = lastRow; row >= firstRow; row--) {
				uint8_t prevBitsHere = image[row][x];
				image[row][x] = (prevBitsHere >> delta) | (carry << (8 - delta));
				carry = prevBitsHere;
			}
		}
	}
}

// Caller must ensure area doesn't go beyond edge of canvas.
// Inverts area inclusive of endY, I think...
void invertArea(int xMin, int width, int startY, int endY, uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	int firstRowY = startY >> 3;
	int lastRowY = endY >> 3;

	uint8_t currentRowMask = (255 << (startY & 7));
	uint8_t lastRowMask = (255 >> (7 - (endY & 7)));

	// For each row
	for (int rowY = firstRowY; rowY <= lastRowY; rowY++) {

		if (rowY == lastRowY) {
			currentRowMask &= lastRowMask;
		}

		uint8_t* __restrict__ currentPos = &image[rowY][xMin];
		uint8_t* const endPos = currentPos + width;

		while (currentPos < endPos) {
			*currentPos ^= currentRowMask;
			currentPos++;
		}

		currentRowMask = 0xFF;
	}
}

void drawGraphicMultiLine(uint8_t const* graphic, int startX, int startY, int width, uint8_t* image, int height,
                          int numBytesTall) {
	int rowOnDisplay = startY >> 3;
	int yOffset = startY & 7;
	int rowOnGraphic = 0;

	if (width > OLED_MAIN_WIDTH_PIXELS - startX) {
		width = OLED_MAIN_WIDTH_PIXELS - startX;
	}

	// First row
	uint8_t* __restrict__ currentPos = &image[rowOnDisplay * OLED_MAIN_WIDTH_PIXELS + startX];
	uint8_t const* endPos = currentPos + width;
	uint8_t const* __restrict__ graphicPos = graphic;

	while (currentPos < endPos) {
		*currentPos |= (*graphicPos << yOffset);
		currentPos++;
		graphicPos += numBytesTall;
	}

	int yOffsetNegative = 8 - yOffset;

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

		currentPos = &image[rowOnDisplay * OLED_MAIN_WIDTH_PIXELS + startX];
		endPos = currentPos + width;
		graphicPos = graphic++; // Takes value before we increment

		if (rowOnGraphic >= numBytesTall) {
			break; // If only drawing that remains is final row of graphic...
		}

		while (currentPos < endPos) {
			// Cleverly read 2 bytes in one go. Doesn't really speed things up. I should try addressing display vertically, so I
			// can do 32 bits in one go on both the graphic and the display...
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

void drawRectangle(int minX, int minY, int maxX, int maxY, uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	drawVerticalLine(minX, minY, maxY, image);
	drawVerticalLine(maxX, minY, maxY, image);

	drawHorizontalLine(minY, minX + 1, maxX - 1, image);
	drawHorizontalLine(maxY, minX + 1, maxX - 1, image);
}

void drawVerticalLine(int pixelX, int startY, int endY, uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	int firstRowY = startY >> 3;
	int lastRowY = endY >> 3;

	uint8_t firstRowMask = (255 << (startY & 7));
	uint8_t lastRowMask = (255 >> (7 - (endY & 7)));

	uint8_t* __restrict__ currentPos = &image[firstRowY][pixelX];

	if (firstRowY == lastRowY) {
		uint8_t mask = firstRowMask & lastRowMask;
		*currentPos |= mask;
	}

	else {
		uint8_t* const finalPos = &image[lastRowY][pixelX];

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

void drawHorizontalLine(int pixelY, int startX, int endX, uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	uint8_t mask = 1 << (pixelY & 7);

	uint8_t* __restrict__ currentPos = &image[pixelY >> 3][startX];
	uint8_t* const lastPos = currentPos + (endX - startX);
	do {
		*currentPos |= mask;
		currentPos++;
	} while (currentPos <= lastPos);
}

void drawString(char const* string, int pixelX, int pixelY, uint8_t* image, int imageWidth, int textWidth,
                int textHeight, int scrollPos, int endX) {
	if (scrollPos) {
		int numCharsToChopOff = (uint16_t)scrollPos / (uint8_t)textWidth;
		if (numCharsToChopOff) {
			if (numCharsToChopOff >= strlen(string)) {
				return;
			}

			string += numCharsToChopOff;
			scrollPos -= numCharsToChopOff * textWidth;
		}
	}
	while (*string) {
		drawChar(*string, pixelX, pixelY, image, imageWidth, textWidth, textHeight, scrollPos, endX);
		pixelX += textWidth - scrollPos;
		if (pixelX >= endX) {
			break;
		}
		scrollPos = 0;
		string++;
	}
}

void drawStringFixedLength(char const* string, int length, int pixelX, int pixelY, uint8_t* image, int imageWidth,
                           int textWidth, int textHeight) {
	char const* const stopAt = string + length;
	while (string < stopAt) {
		drawChar(*string, pixelX, pixelY, image, imageWidth, textWidth, textHeight);
		pixelX += textWidth;
		if (pixelX + textWidth > OLED_MAIN_WIDTH_PIXELS) {
			break;
		}
		string++;
	}
}

void drawStringCentred(char const* string, int pixelY, uint8_t* image, int imageWidth, int textWidth, int textHeight,
                       int centrePos) {
	int length = strlen(string);
	int pixelX = centrePos - ((textWidth * length) >> 1);
	drawStringFixedLength(string, length, pixelX, pixelY, image, imageWidth, textWidth, textHeight);
}

void drawStringAlignRight(char const* string, int pixelY, uint8_t* image, int imageWidth, int textWidth, int textHeight,
                          int rightPos) {
	int length = strlen(string);
	int pixelX = rightPos - (textWidth * length);
	drawStringFixedLength(string, length, pixelX, pixelY, image, imageWidth, textWidth, textHeight);
}

void drawStringCentredShrinkIfNecessary(char const* string, int pixelY, uint8_t* image, int imageWidth, int textWidth,
                                        int textHeight) {
	int length = strlen(string);
	int maxTextWidth = (uint8_t)OLED_MAIN_WIDTH_PIXELS / (unsigned int)length;
	if (textWidth > maxTextWidth) {
		int newHeight = (unsigned int)(textHeight * maxTextWidth) / (unsigned int)textWidth;
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

		int heightDiff = textHeight - newHeight;
		pixelY += heightDiff >> 1;
		textHeight = newHeight;
	}
	int pixelX = (imageWidth - textWidth * length) >> 1;
	drawStringFixedLength(string, length, pixelX, pixelY, image, imageWidth, textWidth, textHeight);
}

#define DO_CHARACTER_SCALING 0

void drawChar(uint8_t theChar, int pixelX, int pixelY, uint8_t* image, int imageWidth, int spacingX, int textHeight,
              int scrollPos, int endX) {

	if (theChar > '~') {
		return;
	}

	if (theChar >= 'a') {
		if (theChar <= 'z') {
			theChar -= 32;
		}
		else {
			theChar -= 26; // Lowercase chars have been snipped out of the tables.
		}
	}

	int charIndex = theChar - 0x20;
	if (charIndex <= 0) {
		return;
	}

	lv_font_glyph_dsc_t const* descriptor;
	uint8_t const* font;
	int fontNativeHeight;

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
	int scaledFontWidth =
	    (uint16_t)(descriptor->w_px * textHeight + (fontNativeHeight >> 1) - 1) / (uint8_t)fontNativeHeight;
#else
	int scaledFontWidth = descriptor->w_px;
#endif
	pixelX += (spacingX - scaledFontWidth) >> 1;

	if (pixelX < 0) {
		scrollPos += -pixelX;
		pixelX = 0;
	}

	int bytesPerCol = ((textHeight - 1) >> 3) + 1;

	int textWidth = descriptor->w_px - scrollPos;
	drawGraphicMultiLine(&font[descriptor->glyph_index + scrollPos * bytesPerCol], pixelX, pixelY, textWidth, image,
	                     textHeight, bytesPerCol);
}

void drawScreenTitle(char const* title) {
	int extraY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 0 : 1;

	int startY = extraY + OLED_MAIN_TOPMOST_PIXEL;

	OLED::drawString(title, 0, startY, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_TITLE_SPACING_X,
	                 TEXT_TITLE_SIZE_Y);
	OLED::drawHorizontalLine(extraY + 11 + OLED_MAIN_TOPMOST_PIXEL, 0, OLED_MAIN_WIDTH_PIXELS - 1,
	                         &OLED::oledMainImage[0]);
}

int oledPopupWidth = 0; // If 0, means popup isn't present / active.
int popupHeight;

int popupMinX;
int popupMaxX;
int popupMinY;
int popupMaxY;

void setupPopup(int width, int height) {
	if (height > OLED_MAIN_HEIGHT_PIXELS) {
		height = OLED_MAIN_HEIGHT_PIXELS;
	}

	oledPopupWidth = width;
	popupHeight = height;

	popupMinX = (OLED_MAIN_WIDTH_PIXELS - oledPopupWidth) >> 1;
	popupMaxX = OLED_MAIN_WIDTH_PIXELS - popupMinX;

	popupMinY = (OLED_MAIN_HEIGHT_PIXELS - popupHeight) >> 1;
	popupMaxY = OLED_MAIN_HEIGHT_PIXELS - popupMinY;

	if (popupMinY < 0) {
		popupMinY = 0;
	}
	if (popupMaxY > OLED_MAIN_HEIGHT_PIXELS - 1) {
		popupMaxY = OLED_MAIN_HEIGHT_PIXELS - 1;
	}

	// Clear the popup's area, not including the rectangle we're about to draw
	int popupFirstRow = (popupMinY + 1) >> 3;
	int popupLastRow = (popupMaxY - 1) >> 3;

	for (int row = popupFirstRow; row <= popupLastRow; row++) {
		memset(&oledMainPopupImage[row][popupMinX + 1], 0, popupMaxX - popupMinX - 1);
	}

	drawRectangle(popupMinX, popupMinY, popupMaxX, popupMaxY, oledMainPopupImage);
}

int consoleMaxX;
int consoleMinX = -1;

struct ConsoleItem {
	uint32_t timeoutTime;
	int16_t minY;
	int16_t maxY;
	bool cleared;
};

#define MAX_NUM_CONSOLE_ITEMS 10

ConsoleItem consoleItems[MAX_NUM_CONSOLE_ITEMS];
int numConsoleItems = 0;

void drawConsoleTopLine() {
	drawHorizontalLine(consoleItems[numConsoleItems - 1].minY - 1, consoleMinX + 1, consoleMaxX - 1,
	                   oledMainConsoleImage);
}

// Returns y position (minY)
int setupConsole(int height) {
	consoleMinX = 4;
	consoleMaxX = OLED_MAIN_WIDTH_PIXELS - consoleMinX;

	bool shouldRedrawTopLine = false;

	// If already some console items...
	if (numConsoleItems) {

		// If hit max num console items...
		if (numConsoleItems == MAX_NUM_CONSOLE_ITEMS) {
			numConsoleItems--;
			shouldRedrawTopLine = true;
		}

		// Shuffle existing console items along
		for (int i = numConsoleItems; i > 0; i--) {
			consoleItems[i] = consoleItems[i - 1];
		}

		// Place new item below existing ones
		consoleItems[0].minY = consoleItems[1].maxY + 1;
		consoleItems[0].maxY = consoleItems[0].minY + height;

		// If that's too low, we'll have to bump the other ones up immediately
		int howMuchTooLow = consoleItems[0].maxY - CONSOLE_IMAGE_HEIGHT + 1;
		if (howMuchTooLow > 0) {

			// Move their min and max values up
			for (int i = numConsoleItems; i >= 0;
			     i--) { // numConsoleItems hasn't been updated yet - there's actually one more
				consoleItems[i].minY -= howMuchTooLow;
				// If at all offscreen, scrap that one
				if (consoleItems[i].minY < 1) {
					numConsoleItems = i - 1; // It's still going to get 1 added to it, below
					shouldRedrawTopLine = true;
				}

				consoleItems[i].maxY -= howMuchTooLow;
			}

			// Do the actual copying
			moveAreaUpCrude(
			    consoleMinX, consoleItems[numConsoleItems].minY - 1, consoleMaxX, consoleItems[1].maxY + howMuchTooLow,
			    howMuchTooLow,
			    oledMainConsoleImage); // numConsoleItems hasn't been updated yet - there's actually one more
		}
	}

	// Or if no other items, easy
	else {
		shouldRedrawTopLine = true;
		consoleItems[0].minY = OLED_MAIN_HEIGHT_PIXELS;
		consoleItems[0].maxY = consoleItems[0].minY + height;
	}

	consoleItems[0].timeoutTime = AudioEngine::audioSampleTimer + 52000; // 1 and a bit seconds
	consoleItems[0].cleared = false;

	numConsoleItems++;

	// Clear the new console item's area
	clearAreaExact(consoleMinX, consoleItems[0].minY, consoleMaxX, consoleItems[0].maxY, oledMainConsoleImage);

	drawVerticalLine(consoleMinX, consoleItems[0].minY - 1, consoleItems[0].maxY, oledMainConsoleImage);
	drawVerticalLine(consoleMaxX, consoleItems[0].minY - 1, consoleItems[0].maxY, oledMainConsoleImage);

	if (shouldRedrawTopLine) {
		drawConsoleTopLine();
	}

	return consoleItems[0].minY;
}

void removePopup() {
	//if (!oledPopupWidth) return;

	oledPopupWidth = 0;
	workingAnimationText = NULL;
	uiTimerManager.unsetTimer(TIMER_DISPLAY);
	sendMainImage();
}

bool isPopupPresent() {
	return oledPopupWidth;
}

void copyRowWithMask(uint8_t destMask, uint8_t sourceRow[], uint8_t destRow[], int minX, int maxX) {
	uint8_t sourceMask = ~destMask;
	uint8_t* __restrict__ source = &sourceRow[minX];
	uint8_t* __restrict__ dest = &destRow[minX];

	uint8_t* const sourceStop = source + maxX - minX;
	do {
		*dest = (*dest & destMask) | (*source & sourceMask);
		dest++;
		source++;
	} while (source <= sourceStop);
}

void copyBackgroundAroundForeground(uint8_t backgroundImage[][OLED_MAIN_WIDTH_PIXELS],
                                    uint8_t foregroundImage[][OLED_MAIN_WIDTH_PIXELS], int minX, int minY, int maxX,
                                    int maxY) {

	// Copy everything above
	int firstRow = minY >> 3;
	int lastRow = maxY >> 3;
	if (firstRow) {
		memcpy(foregroundImage, backgroundImage, OLED_MAIN_WIDTH_PIXELS * firstRow);
	}

	// Partial row above
	int partialRowPixelsAbove = minY & 7;
	if (partialRowPixelsAbove) {
		uint8_t destMask = 255 << partialRowPixelsAbove;
		copyRowWithMask(destMask, backgroundImage[firstRow], foregroundImage[firstRow], minX, maxX);
	}

	// Copy stuff to left and right
	for (int row = firstRow; row <= lastRow; row++) {
		memcpy(foregroundImage[row], backgroundImage[row], minX);
		memcpy(&foregroundImage[row][maxX + 1], &backgroundImage[row][maxX + 1], OLED_MAIN_WIDTH_PIXELS - maxX - 1);
	}

	// Partial row below
	int partialRowPixelsBelow = 7 - (maxY & 7);
	if (partialRowPixelsBelow) {
		uint8_t destMask = 255 >> partialRowPixelsBelow;
		copyRowWithMask(destMask, backgroundImage[lastRow], foregroundImage[lastRow], minX, maxX);
	}

	// Copy everything below
	int numMoreRows = ((OLED_MAIN_HEIGHT_PIXELS - 1) >> 3) - lastRow;
	if (numMoreRows > 0) {
		memcpy(foregroundImage[lastRow + 1], backgroundImage[lastRow + 1], OLED_MAIN_WIDTH_PIXELS * numMoreRows);
	}
}

void sendMainImage() {

	oledCurrentImage = oledMainImage;

	if (numConsoleItems) {
		copyBackgroundAroundForeground(oledMainImage, oledMainConsoleImage, consoleMinX,
		                               consoleItems[numConsoleItems - 1].minY - 1, consoleMaxX,
		                               OLED_MAIN_HEIGHT_PIXELS - 1);
		oledCurrentImage = oledMainConsoleImage;
	}
	if (oledPopupWidth) {
		copyBackgroundAroundForeground(oledCurrentImage, oledMainPopupImage, popupMinX, popupMinY, popupMaxX,
		                               popupMaxY);
		oledCurrentImage = oledMainPopupImage;
	}

#if 0 && ENABLE_TEXT_OUTPUT
	uint16_t renderStopTime = *TCNT[TIMER_SYSTEM_FAST];
	uartPrint("oled render time: ");
	uartPrintNumber((uint16_t)(renderStopTime - renderStartTime));
#endif

	enqueueSPITransfer(0, oledCurrentImage[0]);
}

#define TEXT_MAX_NUM_LINES 8
struct TextLineBreakdown {
	char const* lines[TEXT_MAX_NUM_LINES];
	uint8_t lineLengths[TEXT_MAX_NUM_LINES];
	int numLines;
	int longestLineLength;
	int maxCharsPerLine;
};

void breakStringIntoLines(char const* text, TextLineBreakdown* textLineBreakdown) {
	textLineBreakdown->numLines = 0;
	textLineBreakdown->longestLineLength = 0;

	char const* lineStart = text;
	char const* wordStart = lineStart;
	char const* lineEnd =
	    lineStart
	    + textLineBreakdown
	          ->maxCharsPerLine; // Default to it being the max length - we'll only use this if no "spaces" found.

findNextSpace:
	char const* space = wordStart;
	while (true) {
		if (*space == '\n' || *space == ' ' || *space == 0 || *space == '_') {
			break;
		}
		space++;
	}

lookAtNextSpace:
	int lineLength = (int)space - (int)lineStart;
	if (*space == '_') {
		lineLength++; // If "space" was actually an underscore, include it.
	}

	// If line not too long yet
	if (lineLength <= textLineBreakdown->maxCharsPerLine) {
		lineEnd = space;

		// If end of whole line, or whole string...
		if (*space == '\n' || *space == 0) {
			textLineBreakdown->lines[textLineBreakdown->numLines] = lineStart;
			textLineBreakdown->lineLengths[textLineBreakdown->numLines] = lineLength;
			if (lineLength > textLineBreakdown->longestLineLength) {
				textLineBreakdown->longestLineLength = lineLength;
			}
			textLineBreakdown->numLines++;
			if (!*space || textLineBreakdown->numLines == TEXT_MAX_NUM_LINES) {
				return;
			}
			lineStart = lineEnd + 1;
			lineEnd = lineStart + textLineBreakdown->maxCharsPerLine;
		}
		else if (*space == '_') {
			lineEnd++;
		}

		wordStart = space + 1;
		goto findNextSpace;
	}
	else {
		lineLength = (int)lineEnd - (int)lineStart;
		textLineBreakdown->lines[textLineBreakdown->numLines] = lineStart;
		textLineBreakdown->lineLengths[textLineBreakdown->numLines] = lineLength;
		if (lineLength > textLineBreakdown->longestLineLength) {
			textLineBreakdown->longestLineLength = lineLength;
		}
		textLineBreakdown->numLines++;
		if (textLineBreakdown->numLines == TEXT_MAX_NUM_LINES) {
			return;
		}
		lineStart = lineEnd;
		lineEnd = lineStart + textLineBreakdown->maxCharsPerLine;
		goto lookAtNextSpace;
	}
}

void drawPermanentPopupLookingText(char const* text) {
	TextLineBreakdown textLineBreakdown;
	textLineBreakdown.maxCharsPerLine = 19;

	breakStringIntoLines(text, &textLineBreakdown);

	int textWidth = textLineBreakdown.longestLineLength * TEXT_SPACING_X;
	int textHeight = textLineBreakdown.numLines * TEXT_SPACING_Y;

	int doubleMargin = 12;

	int minX = (OLED_MAIN_WIDTH_PIXELS - textWidth - doubleMargin) >> 1;
	int maxX = OLED_MAIN_WIDTH_PIXELS - minX;

	int minY = (OLED_MAIN_HEIGHT_PIXELS - textHeight - doubleMargin) >> 1;
	int maxY = OLED_MAIN_HEIGHT_PIXELS - minY;

	drawRectangle(minX, minY, maxX, maxY, oledMainImage);

	int textPixelY = (OLED_MAIN_HEIGHT_PIXELS - textHeight) >> 1;
	if (textPixelY < 0) {
		textPixelY = 0;
	}

	for (int l = 0; l < textLineBreakdown.numLines; l++) {
		int textPixelX = (OLED_MAIN_WIDTH_PIXELS - (TEXT_SPACING_X * textLineBreakdown.lineLengths[l])) >> 1;
		drawStringFixedLength(textLineBreakdown.lines[l], textLineBreakdown.lineLengths[l], textPixelX, textPixelY,
		                      oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X, TEXT_SPACING_Y);
		textPixelY += TEXT_SPACING_Y;
	}
}

void popupText(char const* text, bool persistent) {

	TextLineBreakdown textLineBreakdown;
	textLineBreakdown.maxCharsPerLine = 19;

	breakStringIntoLines(text, &textLineBreakdown);

	int textWidth = textLineBreakdown.longestLineLength * TEXT_SPACING_X;
	int textHeight = textLineBreakdown.numLines * TEXT_SPACING_Y;

	int doubleMargin = 12;

	setupPopup(textWidth + doubleMargin, textHeight + doubleMargin);

	int textPixelY = (OLED_MAIN_HEIGHT_PIXELS - textHeight) >> 1;
	if (textPixelY < 0) {
		textPixelY = 0;
	}

	for (int l = 0; l < textLineBreakdown.numLines; l++) {
		int textPixelX = (OLED_MAIN_WIDTH_PIXELS - (TEXT_SPACING_X * textLineBreakdown.lineLengths[l])) >> 1;
		drawStringFixedLength(textLineBreakdown.lines[l], textLineBreakdown.lineLengths[l], textPixelX, textPixelY,
		                      oledMainPopupImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X, TEXT_SPACING_Y);
		textPixelY += TEXT_SPACING_Y;
	}

	sendMainImage();
	if (!persistent) {
		uiTimerManager.setTimer(TIMER_DISPLAY, 800);
	}

	// Or if persistent, make sure no previously set-up timeout occurs.
	else {
		uiTimerManager.unsetTimer(TIMER_DISPLAY);
	}
}

void updateWorkingAnimation() {
	String textNow;
	int error = textNow.set(workingAnimationText);
	if (error) {
		return;
	}

	char buffer[4];
	buffer[3] = 0;

	for (int i = 0; i < 3; i++) {
		buffer[i] = (i <= workingAnimationCount) ? '.' : ' ';
	}

	error = textNow.concatenate(buffer);
	popupText(textNow.get(), true);
}

void displayWorkingAnimation(char const* word) {
	workingAnimationText = word;
	workingAnimationCount = 0;
	updateWorkingAnimation();
}

void removeWorkingAnimation() {
	if (workingAnimationText) {
		removePopup();
	}
}

extern "C" {
void consoleTextIfAllBootedUp(char const* text) {
	if (usbInitializationPeriodComplete) {
		consoleText(text);
	}
}
}

#define CONSOLE_ANIMATION_FRAME_TIME_SAMPLES (6 * 44) // 6

void consoleText(char const* text) {
	TextLineBreakdown textLineBreakdown;
	textLineBreakdown.maxCharsPerLine = 19;

	breakStringIntoLines(text, &textLineBreakdown);

	int charWidth = 6;
	int charHeight = 7;

	int margin = 4;

	int textPixelY = setupConsole(textLineBreakdown.numLines * charHeight + 1) + 1;

	for (int l = 0; l < textLineBreakdown.numLines; l++) {
		int textPixelX = 8;
		drawStringFixedLength(textLineBreakdown.lines[l], textLineBreakdown.lineLengths[l], textPixelX, textPixelY,
		                      oledMainConsoleImage[0], OLED_MAIN_WIDTH_PIXELS, charWidth, charHeight);
		textPixelY += charHeight;
	}

	sendMainImage();

	uiTimerManager.setTimerSamples(TIMER_OLED_CONSOLE, CONSOLE_ANIMATION_FRAME_TIME_SAMPLES);
}

union {
	uint32_t u32;
	struct {
		uint8_t minX;
		uint8_t width;
		uint8_t minY;
		uint8_t maxY;
	};
} blinkArea;

void performBlink() {
	invertArea(blinkArea.minX, blinkArea.width, blinkArea.minY, blinkArea.maxY, oledMainImage);
	sendMainImage();
	uiTimerManager.setTimer(TIMER_OLED_SCROLLING_AND_BLINKING, flashTime);
}

void setupBlink(int minX, int width, int minY, int maxY, bool shouldBlinkImmediately) {
	blinkArea.minX = minX;
	blinkArea.width = width;
	blinkArea.minY = minY;
	blinkArea.maxY = maxY;
	if (shouldBlinkImmediately) {
		invertArea(blinkArea.minX, blinkArea.width, blinkArea.minY, blinkArea.maxY, oledMainImage);
	}
	uiTimerManager.setTimer(TIMER_OLED_SCROLLING_AND_BLINKING, flashTime);
	// Caller must do a sendMainImage() at some point after calling this.
}

void stopBlink() {
	if (blinkArea.u32) {
		blinkArea.u32 = 0;
		uiTimerManager.unsetTimer(TIMER_OLED_SCROLLING_AND_BLINKING);
	}
}

struct SideScroller {
	char const* text; // NULL means not active.
	int textLength;
	int pos;
	int startX;
	int endX;
	int startY;
	int endY;
	int textSpacingX;
	int textSizeY;
	int stringLengthPixels;
	int boxLengthPixels;
	bool finished;
	bool doHilight;
};

#define NUM_SIDE_SCROLLERS 2

SideScroller sideScrollers[NUM_SIDE_SCROLLERS];

void setupSideScroller(int index, char const* text, int startX, int endX, int startY, int endY, int textSpacingX,
                       int textSizeY, bool doHilight) {

	SideScroller* scroller = &sideScrollers[index];
	scroller->textLength = strlen(text);
	scroller->stringLengthPixels = scroller->textLength * textSpacingX;
	scroller->boxLengthPixels = endX - startX;
	if (scroller->stringLengthPixels <= scroller->boxLengthPixels) {
		return;
	}

	scroller->text = text;
	scroller->pos = 0;
	scroller->startX = startX;
	scroller->endX = endX;
	scroller->startY = startY;
	scroller->endY = endY;
	scroller->textSpacingX = textSpacingX;
	scroller->textSizeY = textSizeY;
	scroller->finished = false;
	scroller->doHilight = doHilight;

	sideScrollerDirection = 1;
	uiTimerManager.setTimer(TIMER_OLED_SCROLLING_AND_BLINKING, 400);
}

void stopScrollingAnimation() {
	if (sideScrollerDirection) {
		sideScrollerDirection = 0;
		for (int s = 0; s < NUM_SIDE_SCROLLERS; s++) {
			SideScroller* scroller = &sideScrollers[s];
			scroller->text = NULL;
		}
		uiTimerManager.unsetTimer(TIMER_OLED_SCROLLING_AND_BLINKING);
	}
}

void timerRoutine() {

	if (workingAnimationText) {
		workingAnimationCount = (workingAnimationCount + 1) & 3;
		updateWorkingAnimation();
	}

	else {
		removePopup();
	}
}

void scrollingAndBlinkingTimerEvent() {

	if (blinkArea.u32) {
		performBlink();
		return;
	}

	if (!sideScrollerDirection) {
		return; // Probably isn't necessary...
	}

	bool finished = true;

	for (int s = 0; s < NUM_SIDE_SCROLLERS; s++) {
		SideScroller* scroller = &sideScrollers[s];
		if (scroller->text) {
			if (scroller->finished) {
				continue;
			}

			scroller->pos += sideScrollerDirection;

			if (scroller->pos <= 0) {
				scroller->finished = true;
			}
			else if (scroller->pos >= scroller->stringLengthPixels - scroller->boxLengthPixels) {
				scroller->finished = true;
			}
			else {
				finished = false;
			}

			// Ok, have to render.
			clearAreaExact(scroller->startX, scroller->startY, scroller->endX - 1, scroller->endY, oledMainImage);
			drawString(scroller->text, scroller->startX, scroller->startY, oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
			           scroller->textSpacingX, scroller->textSizeY, scroller->pos, scroller->endX);
			if (scroller->doHilight) {
				invertArea(scroller->startX, scroller->endX - scroller->startX, scroller->startY, scroller->endY,
				           &OLED::oledMainImage[0]);
			}
		}
	}

	sendMainImage();

	int timeInterval;
	if (!finished) {
		timeInterval = (sideScrollerDirection >= 0) ? 15 : 5;
	}
	else {
		timeInterval = 400;
		sideScrollerDirection = -sideScrollerDirection;
		for (int s = 0; s < NUM_SIDE_SCROLLERS; s++) {
			sideScrollers[s].finished = false;
		}
	}
	uiTimerManager.setTimer(TIMER_OLED_SCROLLING_AND_BLINKING, timeInterval);
}

void consoleTimerEvent() {
	// If console active
	if (!numConsoleItems) {
		return;
	}

	int timeTilNext = 0;

	// If console moving up
	if (consoleItems[0].maxY >= OLED_MAIN_HEIGHT_PIXELS) {

		bool anyRemoved = false;

		// Get rid of any items which have hit the top of the screen
		while (consoleItems[numConsoleItems - 1].minY < 2) {
			numConsoleItems--;
			anyRemoved = true;
		}

		if (anyRemoved) {
			drawConsoleTopLine(); // Yeah the line will get copied - it's fine
		}

		int firstRow = (consoleItems[numConsoleItems - 1].minY - 2) >> 3;
		int lastRow = consoleItems[0].maxY >> 3;

		for (int x = consoleMinX; x <= consoleMaxX; x++) {
			uint8_t carry;

			for (int row = lastRow; row >= firstRow; row--) {
				uint8_t prevBitsHere = oledMainConsoleImage[row][x];
				oledMainConsoleImage[row][x] = (prevBitsHere >> 1) | (carry << 7);
				carry = prevBitsHere;
			}
		}

		for (int i = 0; i < numConsoleItems; i++) {
			consoleItems[i].minY--;
			consoleItems[i].maxY--;
		}

		// If got further to go...
		if (consoleItems[0].maxY >= OLED_MAIN_HEIGHT_PIXELS) {
			timeTilNext = CONSOLE_ANIMATION_FRAME_TIME_SAMPLES;
		}
	}

	/*
	// Or if console moving down
	else if (false) {

		int firstRow = consoleItems[numConsoleItems - 1].minY >> 3;
		int lastRow = consoleItems[0].maxY >> 3;

		for (int x = consoleMinX; x <= consoleMaxX; x++) {
			uint8_t carry;

			for (int row = firstRow; row <= lastRow; row++) {
				uint8_t prevBitsHere = oledMainConsoleImage[row][x];
				oledMainConsoleImage[row][x] = (prevBitsHere << 1) | (carry >> 7);
				carry = prevBitsHere;
			}
		}

		for (int i = 0; i < numConsoleItems; i++) {
			consoleItems[i].minY++;
			consoleItems[i].maxY++;
		}

		// If got further to go...
		if (consoleItems[numConsoleItems - 1].minY < OLED_MAIN_HEIGHT_PIXELS) {
			uiTimerManager.setTimer(TIMER_DISPLAY, CONSOLE_ANIMATION_FRAME_TIME_SAMPLES);
		}

		// Or if now fully offscreen...
		else {
			numConsoleItems = 0;
		}
	}
*/
	// If top console item timed out
checkTimeTilTimeout:
	int32_t timeLeft = consoleItems[numConsoleItems - 1].timeoutTime - AudioEngine::audioSampleTimer;
	if (timeLeft <= 0) {
		if (!consoleItems[numConsoleItems - 1].cleared) {
			consoleItems[numConsoleItems - 1].cleared = true;
			clearAreaExact(consoleMinX + 1, consoleItems[numConsoleItems - 1].minY, consoleMaxX - 1,
			               consoleItems[numConsoleItems - 1].maxY, oledMainConsoleImage);
		}
		consoleItems[numConsoleItems - 1].minY++;
		bool shouldCheckAgain = false;
		if (consoleItems[numConsoleItems - 1].minY > consoleItems[numConsoleItems - 1].maxY) {
			numConsoleItems--;
			shouldCheckAgain = numConsoleItems;
		}
		else {
			timeTilNext = CONSOLE_ANIMATION_FRAME_TIME_SAMPLES;
		}
		if (numConsoleItems) {
			drawConsoleTopLine();
		}
		if (shouldCheckAgain) {
			goto checkTimeTilTimeout;
		}
	}

	// Or if it hasn't timed out, then provided we didn't already want a real-soon callback, come back when it does time out
	else if (!timeTilNext) {
		timeTilNext = timeLeft;
	}

	if (timeTilNext) {
		uiTimerManager.setTimerSamples(TIMER_OLED_CONSOLE, timeTilNext);
	}

	sendMainImage();
}

void freezeWithError(char const* text) {
	OLED::clearMainImage();
	int yPixel = OLED_MAIN_TOPMOST_PIXEL;
	OLED::drawString("Error:", 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X,
	                 TEXT_SIZE_Y_UPDATED, 0, OLED_MAIN_WIDTH_PIXELS);
	OLED::drawString(text, TEXT_SPACING_X * 7, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X,
	                 TEXT_SIZE_Y_UPDATED, 0, OLED_MAIN_WIDTH_PIXELS);

	yPixel += TEXT_SPACING_Y;
	OLED::drawString("Press select knob to", 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X,
	                 TEXT_SIZE_Y_UPDATED, 0, OLED_MAIN_WIDTH_PIXELS);

	yPixel += TEXT_SPACING_Y;
	OLED::drawString("attempt resume. Then", 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X,
	                 TEXT_SIZE_Y_UPDATED, 0, OLED_MAIN_WIDTH_PIXELS);

	yPixel += TEXT_SPACING_Y;
	OLED::drawString("save to new file.", 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X,
	                 TEXT_SIZE_Y_UPDATED, 0, OLED_MAIN_WIDTH_PIXELS);

	// Wait for existing DMA transfer to finish
	uint16_t startTime = *TCNT[TIMER_SYSTEM_SLOW];
	while (!(DMACn(OLED_SPI_DMA_CHANNEL).CHSTAT_n & DMAC0_CHSTAT_n_TC)
	       && (uint16_t)(*TCNT[TIMER_SYSTEM_SLOW] - startTime) < msToSlowTimerCount(10)) {}

	// Wait for PIC to de-select OLED, if it's been doing that.
	if (oledWaitingForMessage != 256) {
		startTime = *TCNT[TIMER_SYSTEM_SLOW];
		while ((uint16_t)(*TCNT[TIMER_SYSTEM_SLOW] - startTime) < msToSlowTimerCount(10)) {
			uint8_t value;
			bool anything = uartGetChar(UART_ITEM_PIC, (char*)&value);
			if (anything && value == oledWaitingForMessage) {
				break;
			}
		}
		oledWaitingForMessage = 256;
	}
	spiTransferQueueCurrentlySending = false;

	// Select OLED
	bufferPICUart(248); // Select OLED
	uartFlushIfNotSending(UART_ITEM_PIC);
	oledWaitingForMessage = 248;

	// Wait for selection to be done
	startTime = *TCNT[TIMER_SYSTEM_SLOW];
	while ((uint16_t)(*TCNT[TIMER_SYSTEM_SLOW] - startTime) < msToSlowTimerCount(10)) {
		uint8_t value;
		bool anything = uartGetChar(UART_ITEM_PIC, (char*)&value);
		if (anything && value == 248) {
			break;
		}
	}
	oledWaitingForMessage = 256;

	// Send data via DMA
	RSPI(SPI_CHANNEL_OLED_MAIN).SPDCR = 0x20u;               // 8-bit
	RSPI(SPI_CHANNEL_OLED_MAIN).SPCMD0 = 0b0000011100000010; // 8-bit
	RSPI(SPI_CHANNEL_OLED_MAIN).SPBFCR.BYTE = 0b01100000;    //0b00100000;
	//DMACn(OLED_SPI_DMA_CHANNEL).CHCFG_n = 0b00000000001000000000001001101000 | (OLED_SPI_DMA_CHANNEL & 7);

	int transferSize = (OLED_MAIN_HEIGHT_PIXELS >> 3) * OLED_MAIN_WIDTH_PIXELS;
	DMACn(OLED_SPI_DMA_CHANNEL).N0TB_n = transferSize; // TODO: only do this once?
	uint32_t dataAddress = (uint32_t)OLED::oledMainImage;
	DMACn(OLED_SPI_DMA_CHANNEL).N0SA_n = dataAddress;
	//spiTransferQueueReadPos = (spiTransferQueueReadPos + 1) & (SPI_TRANSFER_QUEUE_SIZE - 1);
	v7_dma_flush_range(dataAddress, dataAddress + transferSize);
	DMACn(OLED_SPI_DMA_CHANNEL).CHCTRL_n |=
	    DMAC_CHCTRL_0S_CLRTC | DMAC_CHCTRL_0S_SETEN; // ---- Enable DMA Transfer and clear TC bit ----

	while (1) {
		uartFlushIfNotSending(UART_ITEM_PIC);
		uartFlushIfNotSending(UART_ITEM_MIDI);

		uint8_t value;
		bool anything = uartGetChar(UART_ITEM_PIC, (char*)&value);
		if (anything) {
			if (value == 175) {
				break;
			}
			else if (value == 249) {}
		}
	}
	oledWaitingForMessage = 256;
	spiTransferQueueCurrentlySending = false;

	clearMainImage();
	popupText("Operation resumed. Save to new file then reboot.");
}

} // namespace OLED

#endif
