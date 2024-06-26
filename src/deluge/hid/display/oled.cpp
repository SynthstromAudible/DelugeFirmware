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

#include "oled.h"
#include "RZA1/mtu/mtu.h"
#include "definitions_cxx.hpp"
#include "drivers/dmac/dmac.h"
#include "drivers/pic/pic.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/hid_sysex.h"
#include "io/debug/log.h"
#include "io/midi/sysex.h"
#include "processing/engines/audio_engine.h"
#include "util/cfunctions.h"
#include "util/d_string.h"
#include <string.h>
#include <string_view>

extern "C" {
#include "RZA1/oled/oled_low_level.h"
#include "RZA1/rspi/rspi.h"
#include "RZA1/uart/sio_char.h"
#include "drivers/oled/oled.h"
#include "gui/fonts/fonts.h"
extern void v7_dma_flush_range(uint32_t start, uint32_t end);
}

extern uint8_t usbInitializationPeriodComplete;

namespace deluge::hid::display {

using ImageStore = oled_canvas::Canvas::ImageStore;

uint8_t (*OLED::oledCurrentImage)[OLED_MAIN_WIDTH_PIXELS];
oled_canvas::Canvas OLED::main;
oled_canvas::Canvas OLED::popup;
oled_canvas::Canvas OLED::console;

bool OLED::needsSending;

int32_t workingAnimationCount;
char const* workingAnimationText; // NULL means animation not active

int32_t sideScrollerDirection; // 0 means none active

const uint8_t OLED::folderIcon[] = {
    0b11111100, 0b10000100, 0b10000110, 0b10000101, 0b10000011, 0b10000001, 0b10000001, 0b11111110,
};

const uint8_t OLED::waveIcon[] = {
    0b00010000, 0b11111110, 0b00111000, 0b00010000, 0b00111000, 0b01111100, 0b00111000, 0b00010000,
};

const uint8_t OLED::songIcon[] = {
    0, 0b01100000, 0b11110000, 0b11110000, 0b01111110, 0b00000110, 0b00000110, 0b00000011, 0b00000011,
};

const uint8_t OLED::synthIcon[] = {0b11111110, 0b11100000, 0b00000000, 0b11111110,
                                   0b00000000, 0b11100000, 0b11111110, 0};

const uint8_t OLED::kitIcon[] = {
    0b00111100, 0b01001010, 0b11110001, 0b10010001, 0b10010001, 0b11110001, 0b01001010, 0b00111100,
};

const uint8_t OLED::downArrowIcon[] = {
    0b00010000, 0b00100000, 0b01111111, 0b00100000, 0b00010000,
};

const uint8_t OLED::rightArrowIcon[] = {
    0b00010101,
    0b00001110,
    0b00000100,
};

// This icon is missing the bottom row because it's 9 pixels tall and bytes only have 8 bits. Remember to draw a
// rectangle under it so it looks correct
const uint8_t OLED::lockIcon[] = {
    0b11111000, //<
    0b11111110, //<
    0b00001001, //<
    0b01111001, //<
    0b01111001, //<
    0b11111110, //<
    0b11111000, //<
};

#if ENABLE_TEXT_OUTPUT
uint16_t renderStartTime;
#endif

void OLED::clearMainImage() {
#if ENABLE_TEXT_OUTPUT
	renderStartTime = *TCNT[TIMER_SYSTEM_FAST];
#endif

	stopBlink();
	stopScrollingAnimation();
	main.clear();
	markChanged();
}

void moveAreaUpCrude(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY, int32_t delta, ImageStore image) {

	int32_t firstRow = minY >> 3;
	int32_t lastRow = maxY >> 3;

	// First move any entire rows
	int32_t deltaRows = delta >> 3;
	if (deltaRows) {
		delta &= 7;
		minY += delta; // There's a bit we can ignore here, potentially
		int32_t firstRowHere = minY >> 3;
		lastRow -= deltaRows;
		for (int32_t row = firstRow; row <= lastRow; row++) {
			memcpy(&image[row][minX], &image[row + deltaRows][minX], maxX - minX + 1);
		}
	}

	// Move final sub-row amount
	if (delta) {
		for (int32_t x = minX; x <= maxX; x++) {
			uint8_t carry;

			for (int32_t row = lastRow; row >= firstRow; row--) {
				uint8_t prevBitsHere = image[row][x];
				image[row][x] = (prevBitsHere >> delta) | (carry << (8 - delta));
				carry = prevBitsHere;
			}
		}
	}
}

int32_t oledPopupWidth = 0; // If 0, means popup isn't present / active.
int32_t popupHeight;
DisplayPopupType popupType = DisplayPopupType::NONE;

int32_t popupMinX;
int32_t popupMaxX;
int32_t popupMinY;
int32_t popupMaxY;

void OLED::setupPopup(int32_t width, int32_t height) {
	if (height > OLED_MAIN_HEIGHT_PIXELS) {
		height = OLED_MAIN_HEIGHT_PIXELS;
	}
	D_PRINTLN("popup with %d x %d", width, height);
	oledPopupWidth = width;
	popupHeight = height;

	popupMinX = (OLED_MAIN_WIDTH_PIXELS - oledPopupWidth) >> 1;
	popupMaxX = OLED_MAIN_WIDTH_PIXELS - popupMinX;

	popupMinY = (OLED_MAIN_HEIGHT_PIXELS - popupHeight) >> 1;
	popupMaxY = OLED_MAIN_HEIGHT_PIXELS - popupMinY;

	if (popupMinY < OLED_MAIN_TOPMOST_PIXEL) {
		popupMinY = OLED_MAIN_TOPMOST_PIXEL;
	}
	if (popupMaxY > OLED_MAIN_HEIGHT_PIXELS - 1) {
		popupMaxY = OLED_MAIN_HEIGHT_PIXELS - 1;
	}

	// Clear the popup's area, not including the rectangle we're about to draw
	int32_t popupFirstRow = (popupMinY + 1) >> 3;
	int32_t popupLastRow = (popupMaxY - 1) >> 3;
	D_PRINTLN("popup dimensions");
	D_PRINTLN("minX %d, minY %d, maxX %d, maxY %d", popupMinX, popupMaxX, popupMinY, popupMaxY);
	popup.clearAreaExact(popupMinX, popupMinY, popupMaxX, popupMaxY);
	popup.drawRectangle(popupMinX, popupMinY, popupMaxX, popupMaxY);
}

int32_t consoleMaxX;
int32_t consoleMinX = -1;

struct ConsoleItem {
	uint32_t timeoutTime;
	int16_t minY;
	int16_t maxY;
	bool cleared;
};

#define MAX_NUM_CONSOLE_ITEMS 4

ConsoleItem consoleItems[MAX_NUM_CONSOLE_ITEMS] = {0};
int32_t numConsoleItems = 0;

void OLED::drawConsoleTopLine() {
	console.drawHorizontalLine(consoleItems[numConsoleItems - 1].minY - 1, consoleMinX + 1, consoleMaxX - 1);
}

// Returns y position (minY)
int32_t OLED::setupConsole(int32_t height) {
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
		for (int32_t i = numConsoleItems; i > 0; i--) {
			consoleItems[i] = consoleItems[i - 1];
		}

		// Place new item below existing ones
		consoleItems[0].minY = consoleItems[1].maxY + 1;
		consoleItems[0].maxY = consoleItems[0].minY + height;

		// If that's too low, we'll have to bump the other ones up immediately
		int32_t howMuchTooLow = consoleItems[0].maxY - kConsoleImageHeight + 1;
		if (howMuchTooLow > 0) {

			// Move their min and max values up
			for (int32_t i = numConsoleItems; i >= 0;
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
			// numConsoleItems hasn't been updated yet - there's actually one more
			moveAreaUpCrude(consoleMinX, consoleItems[numConsoleItems].minY - 1, consoleMaxX,
			                consoleItems[1].maxY + howMuchTooLow, howMuchTooLow, console.hackGetImageStore());
		}
	}

	// Or if no other items, easy
	else {
		shouldRedrawTopLine = true;
		consoleItems[0].minY = OLED_MAIN_HEIGHT_PIXELS - height;
		consoleItems[0].maxY = consoleItems[0].minY + height;
	}

	consoleItems[0].timeoutTime = AudioEngine::audioSampleTimer + 52000; // 1 and a bit seconds
	consoleItems[0].cleared = false;

	numConsoleItems++;

	// Clear the new console item's area
	console.clearAreaExact(consoleMinX, consoleItems[0].minY, consoleMaxX, consoleItems[0].maxY);

	console.drawVerticalLine(consoleMinX, consoleItems[0].minY - 1, consoleItems[0].maxY);
	console.drawVerticalLine(consoleMaxX, consoleItems[0].minY - 1, consoleItems[0].maxY);

	if (shouldRedrawTopLine) {
		drawConsoleTopLine();
	}

	return consoleItems[0].minY;
}

void OLED::removePopup() {
	// if (!oledPopupWidth) return;

	oledPopupWidth = 0;
	popupType = DisplayPopupType::NONE;
	workingAnimationText = NULL;
	uiTimerManager.unsetTimer(TimerName::DISPLAY);
	markChanged();
}

bool OLED::isPopupPresent() {
	return oledPopupWidth;
}
bool OLED::isPopupPresentOfType(DisplayPopupType type) {
	return oledPopupWidth && popupType == type;
}

void copyRowWithMask(uint8_t destMask, uint8_t sourceRow[], uint8_t destRow[], int32_t minX, int32_t maxX) {
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

void copyBackgroundAroundForeground(ImageStore backgroundImage, ImageStore foregroundImage, int32_t minX, int32_t minY,
                                    int32_t maxX, int32_t maxY) {
	if (maxX < 0 || minX < 0 || maxX < minX || minX > OLED_MAIN_WIDTH_PIXELS || maxX > OLED_MAIN_WIDTH_PIXELS)
	    [[unlikely]] {
		FREEZE_WITH_ERROR("C001");
	}
	if (maxY < 0 || minY < 0 || maxY < minY || minY > OLED_MAIN_HEIGHT_PIXELS || maxY > OLED_MAIN_HEIGHT_PIXELS)
	    [[unlikely]] {
		FREEZE_WITH_ERROR("C002");
	}
	// Copy everything above
	int32_t firstRow = minY >> 3;
	int32_t lastRow = maxY >> 3;
	if (firstRow) {
		memcpy(foregroundImage, backgroundImage, OLED_MAIN_WIDTH_PIXELS * firstRow);
	}

	// Partial row above
	int32_t partialRowPixelsAbove = minY & 7;
	if (partialRowPixelsAbove) {
		uint8_t destMask = 255 << partialRowPixelsAbove;
		copyRowWithMask(destMask, backgroundImage[firstRow], foregroundImage[firstRow], minX, maxX);
	}

	// Copy stuff to left and right
	for (int32_t row = firstRow; row <= lastRow; row++) {
		memcpy(foregroundImage[row], backgroundImage[row], minX);
		memcpy(&foregroundImage[row][maxX + 1], &backgroundImage[row][maxX + 1], OLED_MAIN_WIDTH_PIXELS - maxX - 1);
	}

	// Partial row below
	int32_t partialRowPixelsBelow = 7 - (maxY & 7);
	if (partialRowPixelsBelow) {
		uint8_t destMask = 255 >> partialRowPixelsBelow;
		copyRowWithMask(destMask, backgroundImage[lastRow], foregroundImage[lastRow], minX, maxX);
	}

	// Copy everything below
	int32_t numMoreRows = ((OLED_MAIN_HEIGHT_PIXELS - 1) >> 3) - lastRow;
	if (numMoreRows > 0) {
		memcpy(foregroundImage[lastRow + 1], backgroundImage[lastRow + 1], OLED_MAIN_WIDTH_PIXELS * numMoreRows);
	}
}

void OLED::sendMainImage() {
	if (!needsSending) {
		return;
	}

	oledCurrentImage = &main.hackGetImageStore()[0];

	if (numConsoleItems) {
		copyBackgroundAroundForeground(main.hackGetImageStore(), console.hackGetImageStore(), consoleMinX,
		                               consoleItems[numConsoleItems - 1].minY - 1, consoleMaxX,
		                               OLED_MAIN_HEIGHT_PIXELS - 1);
		oledCurrentImage = &console.hackGetImageStore()[0];
	}
	if (oledPopupWidth) {
		copyBackgroundAroundForeground(oledCurrentImage, popup.hackGetImageStore(), popupMinX, popupMinY, popupMaxX,
		                               popupMaxY);
		oledCurrentImage = &popup.hackGetImageStore()[0];
	}

#if OLED_LOG_TIMING
	uint16_t renderStopTime = *TCNT[TIMER_SYSTEM_FAST];
	uartPrint("oled render time: ");
	uartPrintNumber((uint16_t)(renderStopTime - renderStartTime));
#endif

	enqueueSPITransfer(0, oledCurrentImage[0]);
	HIDSysex::sendDisplayIfChanged();
	needsSending = false;
}

#define TEXT_MAX_NUM_LINES 8
struct TextLineBreakdown {
	char const* lines[TEXT_MAX_NUM_LINES];
	uint8_t lineLengths[TEXT_MAX_NUM_LINES];
	int32_t numLines;
	int32_t longestLineLength;
	int32_t maxCharsPerLine;
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
	int32_t lineLength = (int32_t)space - (int32_t)lineStart;
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
		lineLength = (int32_t)lineEnd - (int32_t)lineStart;
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

void OLED::drawPermanentPopupLookingText(char const* text) {
	TextLineBreakdown textLineBreakdown;
	textLineBreakdown.maxCharsPerLine = 19;

	breakStringIntoLines(text, &textLineBreakdown);

	int32_t textWidth = textLineBreakdown.longestLineLength * kTextSpacingX;
	int32_t textHeight = textLineBreakdown.numLines * kTextSpacingY;

	int32_t doubleMargin = 12;

	int32_t minX = (OLED_MAIN_WIDTH_PIXELS - textWidth - doubleMargin) >> 1;
	int32_t maxX = OLED_MAIN_WIDTH_PIXELS - minX;

	int32_t minY = (OLED_MAIN_HEIGHT_PIXELS - textHeight - doubleMargin) >> 1;
	int32_t maxY = OLED_MAIN_HEIGHT_PIXELS - minY;

	main.drawRectangle(minX, minY, maxX, maxY);

	int32_t textPixelY = (OLED_MAIN_HEIGHT_PIXELS - textHeight) >> 1;
	if (textPixelY < 0) {
		textPixelY = 0;
	}

	for (int32_t l = 0; l < textLineBreakdown.numLines; l++) {
		int32_t textPixelX = (OLED_MAIN_WIDTH_PIXELS - (kTextSpacingX * textLineBreakdown.lineLengths[l])) >> 1;
		main.drawString(std::string_view{textLineBreakdown.lines[l], textLineBreakdown.lineLengths[l]}, textPixelX,
		                textPixelY, kTextSpacingX, kTextSpacingY);
		textPixelY += kTextSpacingY;
	}
}

void OLED::popupText(char const* text, bool persistent, DisplayPopupType type) {

	TextLineBreakdown textLineBreakdown;
	textLineBreakdown.maxCharsPerLine = 19;

	breakStringIntoLines(text, &textLineBreakdown);

	int32_t textWidth = textLineBreakdown.longestLineLength * kTextSpacingX;
	int32_t textHeight = textLineBreakdown.numLines * kTextSpacingY;

	int32_t doubleMargin = 12;

	setupPopup(textWidth + doubleMargin, textHeight + doubleMargin);
	popupType = type;

	int32_t textPixelY = (OLED_MAIN_HEIGHT_PIXELS - textHeight) >> 1;
	if (textPixelY < 0) {
		textPixelY = 0;
	}

	for (int32_t l = 0; l < textLineBreakdown.numLines; l++) {
		int32_t textPixelX = (OLED_MAIN_WIDTH_PIXELS - (kTextSpacingX * textLineBreakdown.lineLengths[l])) >> 1;
		popup.drawString(std::string_view{textLineBreakdown.lines[l], textLineBreakdown.lineLengths[l]}, textPixelX,
		                 textPixelY, kTextSpacingX, kTextSpacingY);
		textPixelY += kTextSpacingY;
	}

	markChanged();
	if (!persistent) {
		uiTimerManager.setTimer(TimerName::DISPLAY, 800);
	}

	// Or if persistent, make sure no previously set-up timeout occurs.
	else {
		uiTimerManager.unsetTimer(TimerName::DISPLAY);
	}
}

void updateWorkingAnimation() {
	String textNow;
	Error error = textNow.set(workingAnimationText);
	if (error != Error::NONE) {
		return;
	}

	char buffer[4];
	buffer[3] = 0;

	for (int32_t i = 0; i < 3; i++) {
		buffer[i] = (i <= workingAnimationCount) ? '.' : ' ';
	}

	error = textNow.concatenate(buffer);
	OLED::popupText(textNow.get(), true, DisplayPopupType::LOADING);
}

void OLED::displayWorkingAnimation(char const* word) {
	workingAnimationText = word;
	workingAnimationCount = 0;
	updateWorkingAnimation();
}

void OLED::removeWorkingAnimation() {
	if (hasPopupOfType(DisplayPopupType::LOADING)) {
		removePopup();
	}
	else if (workingAnimationText) {
		workingAnimationText = NULL;
	}
}

void OLED::renderEmulated7Seg(const std::array<uint8_t, kNumericDisplayLength>& display) {
	clearMainImage();
	for (int i = 0; i < 4; i++) {
		int ix = 33 * i + 1;
		const int dy = 17;

		const int horz[] = {6, 0, 3};
		for (int y = 0; y < 3; y++) {
			if (display[i] & (1 << horz[y])) {
				int ybase = 7 + dy * y;
				main.invertArea(ix + 3, 15, ybase + 0, ybase + 0);
				main.invertArea(ix + 2, 17, ybase + 1, ybase + 1);
				main.invertArea(ix + 3, 15, ybase + 2, ybase + 2);
			}
		}

		const int vert[] = {1, 2, 5, 4};
		for (int x = 0; x < 2; x++) {
			int xside = x * 2 - 1;
			for (int y = 0; y < 2; y++) {
				if (display[i] & (1 << vert[2 * x + y])) {
					int xbase = ix + 18 * x + 1;
					int ybase = 10 + dy * y;
					int yside = y * -2 + 1;
					main.invertArea(xbase + xside, 1, ybase + yside, ybase + 13 + yside);
					main.invertArea(xbase, 1, ybase + 0, ybase + 13);
					main.invertArea(xbase - xside, 1, ybase + 1, ybase + 12);
				}
			}
		}

		if (display[i] & (1 << 7)) {
			main.invertArea(ix + 22, 2, 42, 43);
		}
	}
	markChanged();
}

#define CONSOLE_ANIMATION_FRAME_TIME_SAMPLES (6 * 44) // 6

void OLED::consoleText(char const* text) {
	TextLineBreakdown textLineBreakdown;
	textLineBreakdown.maxCharsPerLine = 19;

	breakStringIntoLines(text, &textLineBreakdown);

	int32_t charWidth = 6;
	int32_t charHeight = 7;

	int32_t margin = 4;

	int32_t textPixelY = setupConsole(textLineBreakdown.numLines * charHeight + 1) + 1;

	for (int32_t l = 0; l < textLineBreakdown.numLines; l++) {
		int32_t textPixelX = 8;
		console.drawString(std::string_view{textLineBreakdown.lines[l], textLineBreakdown.lineLengths[l]}, textPixelX,
		                   textPixelY, charWidth, charHeight);
		textPixelY += charHeight;
	}

	markChanged();

	uiTimerManager.setTimerSamples(TimerName::OLED_CONSOLE, CONSOLE_ANIMATION_FRAME_TIME_SAMPLES);
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
	OLED::main.invertArea(blinkArea.minX, blinkArea.width, blinkArea.minY, blinkArea.maxY);
	OLED::markChanged();
	uiTimerManager.setTimer(TimerName::OLED_SCROLLING_AND_BLINKING, kFlashTime);
}

void OLED::setupBlink(int32_t minX, int32_t width, int32_t minY, int32_t maxY, bool shouldBlinkImmediately) {
	blinkArea.minX = minX;
	blinkArea.width = width;
	blinkArea.minY = minY;
	blinkArea.maxY = maxY;
	if (shouldBlinkImmediately) {
		main.invertArea(blinkArea.minX, blinkArea.width, blinkArea.minY, blinkArea.maxY);
	}
	uiTimerManager.setTimer(TimerName::OLED_SCROLLING_AND_BLINKING, kFlashTime);
	markChanged();
}

void OLED::stopBlink() {
	if (blinkArea.u32) {
		blinkArea.u32 = 0;
		uiTimerManager.unsetTimer(TimerName::OLED_SCROLLING_AND_BLINKING);
	}
}

struct SideScroller {
	char const* text; // NULL means not active.
	int32_t textLength;
	int32_t pos;
	int32_t startX;
	int32_t endX;
	int32_t startY;
	int32_t endY;
	int32_t textSpacingX;
	int32_t textSizeY;
	int32_t stringLengthPixels;
	int32_t boxLengthPixels;
	bool finished;
	bool doHighlight;
};

#define NUM_SIDE_SCROLLERS 2

SideScroller sideScrollers[NUM_SIDE_SCROLLERS];

void OLED::setupSideScroller(int32_t index, std::string_view text, int32_t startX, int32_t endX, int32_t startY,
                             int32_t endY, int32_t textSpacingX, int32_t textSizeY, bool doHighlight) {

	SideScroller* scroller = &sideScrollers[index];
	scroller->textLength = text.size();
	scroller->stringLengthPixels = scroller->textLength * textSpacingX;
	scroller->boxLengthPixels = endX - startX;
	if (scroller->stringLengthPixels <= scroller->boxLengthPixels) {
		return;
	}

	scroller->text = text.data();
	scroller->pos = 0;
	scroller->startX = startX;
	scroller->endX = endX;
	scroller->startY = startY;
	scroller->endY = endY;
	scroller->textSpacingX = textSpacingX;
	scroller->textSizeY = textSizeY;
	scroller->finished = false;
	scroller->doHighlight = doHighlight;

	sideScrollerDirection = 1;
	uiTimerManager.setTimer(TimerName::OLED_SCROLLING_AND_BLINKING, kScrollTime);
}

void OLED::stopScrollingAnimation() {
	if (sideScrollerDirection) {
		sideScrollerDirection = 0;
		for (int32_t s = 0; s < NUM_SIDE_SCROLLERS; s++) {
			SideScroller* scroller = &sideScrollers[s];
			scroller->text = NULL;
		}
		uiTimerManager.unsetTimer(TimerName::OLED_SCROLLING_AND_BLINKING);
	}
}

void OLED::timerRoutine() {

	if (workingAnimationText) {
		workingAnimationCount = (workingAnimationCount + 1) & 3;
		updateWorkingAnimation();
	}

	else {
		removePopup();
	}
}

void OLED::scrollingAndBlinkingTimerEvent() {

	if (blinkArea.u32) {
		performBlink();
		return;
	}

	if (!sideScrollerDirection) {
		return; // Probably isn't necessary...
	}

	bool finished = true;

	for (int32_t s = 0; s < NUM_SIDE_SCROLLERS; s++) {
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
			main.clearAreaExact(scroller->startX, scroller->startY, scroller->endX - 1, scroller->endY);
			main.drawString(scroller->text, scroller->startX, scroller->startY, scroller->textSpacingX,
			                scroller->textSizeY, scroller->pos, scroller->endX);
			if (scroller->doHighlight) {
				main.invertArea(scroller->startX, scroller->endX - scroller->startX, scroller->startY, scroller->endY);
			}
		}
	}

	markChanged();

	int32_t timeInterval;
	if (!finished) {
		timeInterval = (sideScrollerDirection >= 0) ? 15 : 5;
	}
	else {
		timeInterval = kScrollTime;
		sideScrollerDirection = -sideScrollerDirection;
		for (int32_t s = 0; s < NUM_SIDE_SCROLLERS; s++) {
			sideScrollers[s].finished = false;
		}
	}
	uiTimerManager.setTimer(TimerName::OLED_SCROLLING_AND_BLINKING, timeInterval);
}

void OLED::consoleTimerEvent() {
	// If console active
	if (!numConsoleItems) {
		return;
	}

	int32_t timeTilNext = 0;

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

		int32_t firstRow = (consoleItems[numConsoleItems - 1].minY - 2) >> 3;
		int32_t lastRow = consoleItems[0].maxY >> 3;

		for (int32_t x = consoleMinX; x <= consoleMaxX; x++) {
			uint8_t carry;

			for (int32_t row = lastRow; row >= firstRow; row--) {
				uint8_t prevBitsHere = console.hackGetImageStore()[row][x];
				console.hackGetImageStore()[row][x] = (prevBitsHere >> 1) | (carry << 7);
				carry = prevBitsHere;
			}
		}

		for (int32_t i = 0; i < numConsoleItems; i++) {
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

	    int32_t firstRow = consoleItems[numConsoleItems - 1].minY >> 3;
	    int32_t lastRow = consoleItems[0].maxY >> 3;

	    for (int32_t x = consoleMinX; x <= consoleMaxX; x++) {
	        uint8_t carry;

	        for (int32_t row = firstRow; row <= lastRow; row++) {
	            uint8_t prevBitsHere = oledMainConsoleImage[row][x];
	            oledMainConsoleImage[row][x] = (prevBitsHere << 1) | (carry >> 7);
	            carry = prevBitsHere;
	        }
	    }

	    for (int32_t i = 0; i < numConsoleItems; i++) {
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
			console.clearAreaExact(consoleMinX + 1, consoleItems[numConsoleItems - 1].minY, consoleMaxX - 1,
			                       consoleItems[numConsoleItems - 1].maxY);
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

	// Or if it hasn't timed out, then provided we didn't already want a real-soon callback, come back when it does time
	// out
	else if (!timeTilNext) {
		timeTilNext = timeLeft;
	}

	if (timeTilNext) {
		uiTimerManager.setTimerSamples(TimerName::OLED_CONSOLE, timeTilNext);
	}

	markChanged();
}

void OLED::freezeWithError(char const* text) {
	OLED::clearMainImage();
	int32_t yPixel = OLED_MAIN_TOPMOST_PIXEL;
	main.drawString("Error:", 0, yPixel, kTextSpacingX, kTextSizeYUpdated, 0, OLED_MAIN_WIDTH_PIXELS);
	main.drawString(text, kTextSpacingX * 7, yPixel, kTextSpacingX, kTextSizeYUpdated, 0, OLED_MAIN_WIDTH_PIXELS);

	yPixel += kTextSpacingY;
	main.drawString("Press select knob to", 0, yPixel, kTextSpacingX, kTextSizeYUpdated, 0, OLED_MAIN_WIDTH_PIXELS);

	yPixel += kTextSpacingY;
	main.drawString("attempt resume. Then", 0, yPixel, kTextSpacingX, kTextSizeYUpdated, 0, OLED_MAIN_WIDTH_PIXELS);

	yPixel += kTextSpacingY;
	main.drawString("save to new file.", 0, yPixel, kTextSpacingX, kTextSizeYUpdated, 0, OLED_MAIN_WIDTH_PIXELS);

	// Wait for existing DMA transfer to finish
	uint16_t startTime = *TCNT[TIMER_SYSTEM_SLOW];
	while (!(DMACn(OLED_SPI_DMA_CHANNEL).CHSTAT_n & DMAC0_CHSTAT_n_TC)
	       && (uint16_t)(*TCNT[TIMER_SYSTEM_SLOW] - startTime) < msToSlowTimerCount(50)) {}

	// Wait for PIC to de-select OLED, if it's been doing that.
	if (oledWaitingForMessage != 256) {
		startTime = *TCNT[TIMER_SYSTEM_SLOW];
		while ((uint16_t)(*TCNT[TIMER_SYSTEM_SLOW] - startTime) < msToSlowTimerCount(50)) {
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
	PIC::selectOLED();
	PIC::flush();
	oledWaitingForMessage = 248;

	// Wait for selection to be done
	startTime = *TCNT[TIMER_SYSTEM_SLOW];
	while ((uint16_t)(*TCNT[TIMER_SYSTEM_SLOW] - startTime) < msToSlowTimerCount(50)) {
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
	RSPI(SPI_CHANNEL_OLED_MAIN).SPBFCR.BYTE = 0b01100000;    // 0b00100000;
	// DMACn(OLED_SPI_DMA_CHANNEL).CHCFG_n = 0b00000000001000000000001001101000 | (OLED_SPI_DMA_CHANNEL & 7);

	int32_t transferSize = (OLED_MAIN_HEIGHT_PIXELS >> 3) * OLED_MAIN_WIDTH_PIXELS;
	DMACn(OLED_SPI_DMA_CHANNEL).N0TB_n = transferSize; // TODO: only do this once?
	uint32_t dataAddress = (uint32_t)(&OLED::main.hackGetImageStore()[0][0]);
	DMACn(OLED_SPI_DMA_CHANNEL).N0SA_n = dataAddress;
	// spiTransferQueueReadPos = (spiTransferQueueReadPos + 1) & (SPI_TRANSFER_QUEUE_SIZE - 1);
	v7_dma_flush_range(dataAddress, dataAddress + transferSize);
	DMACn(OLED_SPI_DMA_CHANNEL).CHCTRL_n |=
	    DMAC_CHCTRL_0S_CLRTC | DMAC_CHCTRL_0S_SETEN; // ---- Enable DMA Transfer and clear TC bit ----

	while (1) {
		PIC::flush();
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
	OLED::popupText("Operation resumed. Save to new file then reboot.", false, DisplayPopupType::GENERAL);
}

extern std::string_view getErrorMessage(Error);

void OLED::displayError(Error error) {
	char const* message = nullptr;
	switch (error) {
	case Error::NONE:
	case Error::ABORTED_BY_USER:
		return;
	default:
		message = getErrorMessage(error).data();
		break;
	}
	displayPopup(message);
}

} // namespace deluge::hid::display
