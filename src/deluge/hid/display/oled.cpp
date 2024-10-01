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
#include "deluge/playback/playback_handler.h"
#include "drivers/dmac/dmac.h"
#include "drivers/pic/pic.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/hid_sysex.h"
#include "io/debug/log.h"
#include "io/midi/sysex.h"
#include "processing/engines/audio_engine.h"
#include "storage/flash_storage.h"
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
    0b11111100, //<
    0b10000100, //<
    0b10000110, //<
    0b10000101, //<
    0b10000011, //<
    0b10000001, //<
    0b10000001, //<
    0b11111110, //<
};

const uint8_t OLED::waveIcon[] = {
    0b00010000, //<
    0b11111110, //<
    0b00111000, //<
    0b00010000, //<
    0b00111000, //<
    0b01111100, //<
    0b00111000, //<
    0b00010000, //<
};

const uint8_t OLED::songIcon[] = {
    0b00000000, //<
    0b01100000, //<
    0b11110000, //<
    0b11110000, //<
    0b01111110, //<
    0b00000110, //<
    0b00000110, //<
    0b00000011, //<
    0b00000011, //<
};

const uint8_t OLED::synthIcon[] = {
    0b11111110, //<
    0b11100000, //<
    0b00000000, //<
    0b11111110, //<
    0b00000000, //<
    0b11100000, //<
    0b11111110, //<
    0b00000000, //<
};

const uint8_t OLED::kitIcon[] = {
    0b00111100, //<
    0b01001010, //<
    0b11110001, //<
    0b10010001, //<
    0b10010001, //<
    0b11110001, //<
    0b01001010, //<
    0b00111100, //<
};

// midi icon is 9 pixels wide, icons are normally 8 pixels wide
// to accomodate rendering that right edge, a second icon is added below
// to be rendered right after this one
const uint8_t OLED::midiIcon[] = {
    0b01111100, //<
    0b10000010, //<
    0b00101001, //<
    0b10000001, //<
    0b10000101, //<
    0b10000001, //<
    0b00101001, //<
    0b10000010, //<
};

const uint8_t OLED::midiIconPt2[] = {
    0b01111100, //<
    0b00000000, //<
    0b00000000, //<
    0b00000000, //<
    0b00000000, //<
    0b00000000, //<
    0b00000000, //<
    0b00000000, //<
};

const uint8_t OLED::downArrowIcon[] = {
    0b00010000, //<
    0b00100000, //<
    0b01111111, //<
    0b00100000, //<
    0b00010000, //<
};

const uint8_t OLED::rightArrowIcon[] = {
    0b00010101, //<
    0b00001110, //<
    0b00000100, //<
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

const uint8_t OLED::checkedBoxIcon[] = {
    0b11111110, //<
    0b10000010, //<
    0b10011010, //<
    0b10110010, //<
    0b10011010, //<
    0b10001100, //<
    0b11100110, //<
};

const uint8_t OLED::uncheckedBoxIcon[] = {
    0b11111110, //<
    0b10000010, //<
    0b10000010, //<
    0b10000010, //<
    0b10000010, //<
    0b10000010, //<
    0b11111110, //<
};

const uint8_t OLED::submenuArrowIcon[] = {
    0b00000000, //<
    0b00000000, //<
    0b01000100, //<
    0b00101000, //<
    0b00010000, //<
    0b00000000, //<
    0b00000000, //<
};

const uint8_t OLED::metronomeIcon[] = {
    0b11100000, //<
    0b11011100, //<
    0b11000011, //<
    0b11100001, //<
    0b11010011, //<
    0b11011000, //<
    0b11100100, //<
};

#if ENABLE_TEXT_OUTPUT
uint16_t renderStartTime;
#endif

bool drawnPermanentPopup = false;

void OLED::clearMainImage() {
#if ENABLE_TEXT_OUTPUT
	renderStartTime = *TCNT[TIMER_SYSTEM_FAST];
#endif

	stopBlink();
	stopScrollingAnimation();
	main.clear();
	markChanged();
	drawnPermanentPopup = false;
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
PopupType popupType = PopupType::NONE;

int32_t popupMinX;
int32_t popupMaxX;
int32_t popupMinY;
int32_t popupMaxY;

void OLED::setupPopup(int32_t width, int32_t height) {
	if (height > OLED_MAIN_HEIGHT_PIXELS) {
		height = OLED_MAIN_HEIGHT_PIXELS;
	}
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

ConsoleItem consoleItemStoreDontAccessDirectly[MAX_NUM_CONSOLE_ITEMS] = {0};
int32_t numConsoleItems = 0;

// There is suspicion that we're under or overflowing the memory for the consoleItems
// array: so we've renamed it to consoleItemStoreDontAccessDirectly, and have this class
// act as a guard to catch out of bounds accesses.
//
// See: https://github.com/SynthstromAudible/DelugeFirmware/issues/2151
//
// D001 and D002 are trying to catch the same problem, but after it happens.
class ConsoleItemAccessor {
public:
	ConsoleItem& operator[](size_t index) {
		if (index < 0 || MAX_NUM_CONSOLE_ITEMS <= index) [[unlikely]] {
			FREEZE_WITH_ERROR("D003");
		}
		return consoleItemStoreDontAccessDirectly[index];
	}
};

ConsoleItemAccessor consoleItems;

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
			for (int32_t i = numConsoleItems; i >= 0; i--) {
				// numConsoleItems hasn't been updated yet - there's actually one more
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
		// Sean: -1 adjustment here due to some visual artifacts observed with the console rendering
		//		 this shifts all console items up a pixel
		consoleItems[0].minY = OLED_MAIN_HEIGHT_PIXELS - height - 1;
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
	popupType = PopupType::NONE;
	workingAnimationText = NULL;
	uiTimerManager.unsetTimer(TimerName::DISPLAY);
	markChanged();
}

bool OLED::isPopupPresent() {
	return oledPopupWidth;
}
bool OLED::isPopupPresentOfType(PopupType type) {
	return oledPopupWidth && popupType == type;
}

bool OLED::isPermanentPopupPresent() {
	return drawnPermanentPopup;
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
		// D for Display
		FREEZE_WITH_ERROR("D001");
	}
	if (maxY < 0 || minY < 0 || maxY < minY || minY > OLED_MAIN_HEIGHT_PIXELS || maxY > OLED_MAIN_HEIGHT_PIXELS)
	    [[unlikely]] {
		FREEZE_WITH_ERROR("D002");
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
	uint32_t lineWidths[TEXT_MAX_NUM_LINES];
	int32_t numLines;
	int32_t longestLineLength;
	int32_t maxCharsPerLine;
	int32_t longestLineWidth;
	int32_t maxWidthPerLine;
};

/// adds character to line by increasing line width (in px) and line length (in characters)
/// returns the following:
/// 1) updated line width in pixels
/// 2) updated line length in characters
/// 3) number of spacing in pixels added after the last character
/// 4) whether the character added extends the line width past the max width allowed
bool addCharacterToLine(char c, int32_t maxWidthPerLine, int32_t textHeight, int32_t& lineWidth, int32_t& lineLength,
                        int32_t& charSpacing) {
	// we're in a word, calculate width (in px) of the character in that word
	int32_t charWidth = deluge::hid::display::OLED::popup.getCharWidthInPixels(c, textHeight);

	// increase line width for the character added
	lineWidth += charWidth;

	// add spacing (not relevant if you're using a monospaced font, which we are here, but keep it anyway in case we
	// don't)
	charSpacing = deluge::hid::display::OLED::popup.getCharSpacingInPixels(c, textHeight, false);
	lineWidth += charSpacing;

	// increment the number of characters in this line
	lineLength++;

	// does the character we just added cause us to go over the line width?
	// if no, return true so we can keep adding characters
	if (lineWidth <= maxWidthPerLine) {
		return true;
	}
	// if yes, return false so we stop looking at characters
	else {
		return false;
	}
}

/// used with breakStringIntoLines function below
/// here we iterate through every character in the string until we reach a "break" in a word
/// which can be due to a new line, a space, an underscore or the end of the string (0)
/// returns the following:
/// 1) the string remaining after the split point
/// 2) updated textLineBreakdown struct
/// 3) width of the line in pixels from start of the line up to the split point
/// 4) returns number of characters from start of the line up to the split point
/// 5) returns number of spacing in pixels added after the last character
char const* findLineSplitPoint(char const* wordStart, int32_t maxWidthPerLine, int32_t textHeight, int32_t& lineWidth,
                               int32_t& lineLength, int32_t& charSpacing) {
	bool lineWidthLimitReached = false;
	char const* space = wordStart;
	while (true) {
		// we've reached end of a word
		if (*space == '\n' || *space == ' ' || *space == 0 || *space == '_') {
			break;
		}
		// add character and check if the character we just aded takes us over the line width
		if (!addCharacterToLine(*space, maxWidthPerLine, textHeight, lineWidth, lineLength, charSpacing)) {
			// character took us over line width, so return here
			return space;
		}
		space++; // increment to see what next character is
	}

	// we're here because we reached end of a word
	if (*space == '_' || *space == ' ') {
		addCharacterToLine(*space, maxWidthPerLine, textHeight, lineWidth, lineLength, charSpacing);
	}
	return space;
}

void addLine(TextLineBreakdown* textLineBreakdown, char const* lineStart, int32_t lineWidth, int32_t lineLength) {
	// save the start position for this line of the string
	textLineBreakdown->lines[textLineBreakdown->numLines] = lineStart;
	// save the length in characters for this line of the string
	textLineBreakdown->lineLengths[textLineBreakdown->numLines] = lineLength;
	// check if this line is the longest line in characters, if so, save the length
	if (lineLength > textLineBreakdown->longestLineLength) {
		textLineBreakdown->longestLineLength = lineLength;
	}
	// save the length in pixels for this line of the string
	textLineBreakdown->lineWidths[textLineBreakdown->numLines] = lineWidth;
	// check if this line is the longest line in pixels, if so, save the width in pixels
	if (lineWidth > textLineBreakdown->longestLineWidth) {
		textLineBreakdown->longestLineWidth = lineWidth;
	}
	// increment number of lines
	textLineBreakdown->numLines++;
}

/// this function takes a string and breaks it into multiple lines
/// it does so by iterating through each character in a string, calculating the width in pixels
/// for each character and comparing that width to the maximum width of a line
/// as characters are processed, the width and length of a given line is incremented so that it can be
/// compared against. If a character cannot be drawn because a line's width and length is reached
/// then that character is evaluated to determine what to do with it
/// for example:
/// a) if the character is part of a word, then that word will be rendered on the next line
/// b) if the character is a space, then the space gets ignored and the next character will be drawn on
///    the next line
void breakStringIntoLines(char const* text, TextLineBreakdown* textLineBreakdown, int32_t textHeight) {
	textLineBreakdown->numLines = 0;
	textLineBreakdown->longestLineLength = 0;
	textLineBreakdown->longestLineWidth = 0;

	char const* lineStart = text;
	char const* wordStart = lineStart;

	int32_t lineWidth = 0;                // width in pixels of a line
	int32_t lineLength = 0;               // number of characters in a line
	int32_t lineWidthBeforeThisWord = 0;  // line width in pixels up to the current word
	int32_t lineLengthBeforeThisWord = 0; // line length in characters up to the current word
	int32_t charSpacing = 0;              // number of pixels last added after a character

findNextStringSplitPoint:
	// save current lineWidth / lineLength in case we need to cut off drawing on a line before current word
	lineWidthBeforeThisWord = lineWidth;
	lineLengthBeforeThisWord = lineLength;

	char const* space = findLineSplitPoint(wordStart, textLineBreakdown->maxWidthPerLine, textHeight, lineWidth,
	                                       lineLength, charSpacing);

	// If line not too long yet
	if (lineWidth <= textLineBreakdown->maxWidthPerLine) {
		// If end of whole line, or whole string...
		if (*space == '\n' || *space == 0) {
			// if we reached line break or end of string, we don't need extra spacing after it
			lineWidth -= charSpacing;
			addLine(textLineBreakdown, lineStart, lineWidth, lineLength);
			// did we reach the end of the original string? or the max number of lines?
			// then return, we're done
			if (!*space || textLineBreakdown->numLines == TEXT_MAX_NUM_LINES) {
				return;
			}
			// set character to start drawing from on the next line
			lineStart = space + 1;
			// reset line width and line length as we'll start counting from 0 for the next line
			lineWidth = 0;
			lineLength = 0;
			charSpacing = 0;
		}
		// set character to start iterating from on
		wordStart = space + 1;

		// continue iterating through remaining characters in the string
		goto findNextStringSplitPoint;
	}
	// line width exceeds maximum
	else {
		// if we reached line break or end of string, we don't need extra spacing after it
		lineWidthBeforeThisWord -= charSpacing;
		addLine(textLineBreakdown, lineStart, lineWidthBeforeThisWord, lineLengthBeforeThisWord);
		// did we reach the the max number of lines?
		// then return, we're done
		if (textLineBreakdown->numLines == TEXT_MAX_NUM_LINES) {
			return;
		}
		// if current character is a space, we won't draw it on the next line
		// so let's move forward a character
		if (*space == ' ') {
			wordStart = lineStart = space + 1;
		}
		// otherwise let's render the current word on the next line because it's too long for this line
		else {
			lineStart = wordStart;
		}
		// reset line width and line length as we'll start counting from 0 for the next line
		lineWidth = 0;
		lineLength = 0;
		charSpacing = 0;

		// continue iterating through remaining characters in the string
		goto findNextStringSplitPoint;
	}
}

void OLED::drawPermanentPopupLookingText(char const* text) {
	TextLineBreakdown textLineBreakdown;
	textLineBreakdown.maxCharsPerLine = 19;

	int32_t doubleMargin = 12;
	textLineBreakdown.maxWidthPerLine = OLED_MAIN_WIDTH_PIXELS - doubleMargin;

	breakStringIntoLines(text, &textLineBreakdown, kTextSpacingY);

	int32_t textWidth = textLineBreakdown.longestLineWidth;
	int32_t textHeight = textLineBreakdown.numLines * kTextSpacingY;

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
		int32_t textPixelX = (OLED_MAIN_WIDTH_PIXELS - textLineBreakdown.lineWidths[l]) >> 1;
		main.drawString(std::string_view{textLineBreakdown.lines[l], textLineBreakdown.lineLengths[l]}, textPixelX,
		                textPixelY, kTextSpacingX, kTextSpacingY);
		textPixelY += kTextSpacingY;
	}

	drawnPermanentPopup = true;
}

void OLED::popupText(char const* text, bool persistent, PopupType type) {

	TextLineBreakdown textLineBreakdown;
	textLineBreakdown.maxCharsPerLine = 19;

	int32_t doubleMargin = 12;
	textLineBreakdown.maxWidthPerLine = OLED_MAIN_WIDTH_PIXELS - doubleMargin;

	breakStringIntoLines(text, &textLineBreakdown, kTextSpacingY);

	int32_t textWidth = textLineBreakdown.longestLineWidth;
	int32_t textHeight = textLineBreakdown.numLines * kTextSpacingY;

	setupPopup(textWidth + doubleMargin, textHeight + doubleMargin);
	popupType = type;

	int32_t textPixelY = (OLED_MAIN_HEIGHT_PIXELS - textHeight) >> 1;
	if (textPixelY < 0) {
		textPixelY = 0;
	}

	for (int32_t l = 0; l < textLineBreakdown.numLines; l++) {
		if (textPixelY >= OLED_MAIN_HEIGHT_PIXELS) {
			continue;
		}
		int32_t textPixelX = (OLED_MAIN_WIDTH_PIXELS - textLineBreakdown.lineWidths[l]) >> 1;
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
	OLED::popupText(textNow.get(), true, PopupType::LOADING);
}

void OLED::displayWorkingAnimation(char const* word) {
	workingAnimationText = word;
	workingAnimationCount = 0;
	updateWorkingAnimation();
}

void OLED::removeWorkingAnimation() {
	if (hasPopupOfType(PopupType::LOADING)) {
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
			main.invertArea(ix + 21, 3, 41, 43);
		}
	}
	markChanged();
}

#define CONSOLE_ANIMATION_FRAME_TIME_SAMPLES (6 * 44) // 6

void OLED::consoleText(char const* text) {
	TextLineBreakdown textLineBreakdown;
	textLineBreakdown.maxCharsPerLine = 19;

	int32_t textPixelX = 8;
	textLineBreakdown.maxWidthPerLine = OLED_MAIN_WIDTH_PIXELS - textPixelX;

	int32_t charWidth = 6;
	int32_t charHeight = 7;

	breakStringIntoLines(text, &textLineBreakdown, charHeight);

	int32_t textPixelY = setupConsole(textLineBreakdown.numLines * charHeight + 1) + 1;

	for (int32_t l = 0; l < textLineBreakdown.numLines; l++) {
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
	bool scrollDuringPlayback;
};

#define NUM_SIDE_SCROLLERS 2

SideScroller sideScrollers[NUM_SIDE_SCROLLERS];

void OLED::setupSideScroller(int32_t index, std::string_view text, int32_t startX, int32_t endX, int32_t startY,
                             int32_t endY, int32_t textSpacingX, int32_t textSizeY, bool doHighlight,
                             bool scrollDuringPlayback) {

	SideScroller* scroller = &sideScrollers[index];
	scroller->textLength = text.size();

	scroller->stringLengthPixels = 0;

	int32_t charIdx = 0;
	for (char const c : text) {
		int32_t charSpacing = main.getCharSpacingInPixels(c, textSizeY, charIdx == scroller->textLength);
		int32_t charWidth = main.getCharWidthInPixels(c, textSizeY) + charSpacing;
		scroller->stringLengthPixels += charWidth;
		charIdx++;
	}

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
	scroller->textSpacingX = scroller->stringLengthPixels / scroller->textLength;
	scroller->textSizeY = textSizeY;
	scroller->finished = false;
	scroller->doHighlight = doHighlight;
	scroller->scrollDuringPlayback = scrollDuringPlayback;

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
	bool didAnyRendering = false;
	bool didAnyScrolling = true;

	for (int32_t s = 0; s < NUM_SIDE_SCROLLERS; s++) {
		SideScroller* scroller = &sideScrollers[s];
		if (scroller->text) {
			if (scroller->finished) {
				continue;
			}

			bool refreshDisplay = true;

			// if playback is not running or it's running and you allow scrolling then scroll text
			if (!playbackHandler.isEitherClockActive()
			    || (playbackHandler.isEitherClockActive() && scroller->scrollDuringPlayback)) {

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
			}
			// otherwise, reset scroller to the beginning (if it's not already there)
			else {
				// no need to refresh display if scroller is already at pos 0
				if (scroller->pos == 0) {
					refreshDisplay = false;
				}
				scroller->pos = 0;
				sideScrollerDirection = 1; // so we scroll forward if you turn playback off
				didAnyScrolling = false;
			}

			int32_t endX = scroller->endX;
			if (FlashStorage::accessibilityMenuHighlighting) {
				// for submenu's, this is the padding before the icon's are rendered
				// need to clear this area otherwise it leaves a white pixels
				endX += 4;
			}

			if (refreshDisplay) {
				// Ok, have to render.
				main.clearAreaExact(scroller->startX, scroller->startY, endX - 1, scroller->endY);
				main.drawString(scroller->text, scroller->startX, scroller->startY, scroller->textSpacingX,
				                scroller->textSizeY, scroller->pos, scroller->endX);
				if (scroller->doHighlight && !FlashStorage::accessibilityMenuHighlighting) {
					main.invertArea(scroller->startX, scroller->endX - scroller->startX, scroller->startY,
					                scroller->endY);
				}
				didAnyRendering = true;
			}
		}
	}

	if (didAnyRendering) {
		markChanged();
		// Workaround for glitches during scrolling. Not entirely obvious _why_
		// it glitches, though. Some sort of timing issue between doAnyPendingUIRendering,
		// uiTimerManager.routine() ?
		sendMainImage();
	}

	int32_t timeInterval;
	if (!finished) {
		timeInterval = (sideScrollerDirection >= 0) ? 15 : 5;
	}
	else {
		timeInterval = kScrollTime;
		// if we didn't do any scrolling, then no need to change scroll direction
		if (didAnyScrolling) {
			sideScrollerDirection = -sideScrollerDirection;
		}
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
	OLED::popupText("Operation resumed. Save to new file then reboot.", false, PopupType::GENERAL);
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
	D_PRINTLN(message);
}

} // namespace deluge::hid::display
