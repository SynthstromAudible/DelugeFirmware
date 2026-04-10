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
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/flash_storage.h"
#include "util/cfunctions.h"
#include "util/d_string.h"
#include <algorithm>
#include <bits/ranges_algo.h>
#include <cstring>
#include <etl/vector.h>
#include <gui/ui/ui.h>
#include <ranges>
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

static int32_t working_animation_count;
static bool started_animation;
static bool loading;

int32_t sideScrollerDirection; // 0 means none active

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

void OLED::setupPopup(PopupType type, int32_t width, int32_t height, std::optional<int32_t> startX,
                      std::optional<int32_t> startY) {
	height = std::clamp<int32_t>(height, 0, OLED_MAIN_HEIGHT_PIXELS);
	oledPopupWidth = width;
	popupHeight = height;
	popupType = type;

	popupMinX = startX.has_value() ? startX.value() : (OLED_MAIN_WIDTH_PIXELS - width) / 2;
	popupMaxX = popupMinX + width;
	popupMinY = startY.has_value() ? startY.value() : (OLED_MAIN_HEIGHT_PIXELS - height) / 2;
	popupMaxY = popupMinY + height;
#if ALPHA_OR_BETA_VERSION
	if (popupMaxX < 0 || popupMinX < 0 || popupMaxX < popupMinX || popupMinX > OLED_MAIN_WIDTH_PIXELS
	    || popupMaxX > OLED_MAIN_WIDTH_PIXELS) {

		uint32_t regLR = 0;
		uint32_t regSP = 0;
		asm volatile("MOV %0, LR\n" : "=r"(regLR));
		asm volatile("MOV %0, SP\n" : "=r"(regSP));
		fault_handler_print_freeze_pointers(0, 0, regLR, regSP);
		::freezeWithError("D003");
	}
	if (popupMaxY < 0 || popupMinY < 0 || popupMaxY < popupMinY || popupMinY > OLED_MAIN_WIDTH_PIXELS
	    || popupMaxY > OLED_MAIN_WIDTH_PIXELS) {

		uint32_t regLR = 0;
		uint32_t regSP = 0;
		asm volatile("MOV %0, LR\n" : "=r"(regLR));
		asm volatile("MOV %0, SP\n" : "=r"(regSP));
		fault_handler_print_freeze_pointers(0, 0, regLR, regSP);
		::freezeWithError("D003");
	}
#endif
	popup.clearAreaExact(popupMinX, popupMinY, popupMaxX, popupMaxY);

	if (type != PopupType::NOTIFICATION) {
		popup.drawRectangleRounded(popupMinX, popupMinY, popupMaxX, popupMaxY);
	}
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
	oledPopupWidth = 0;
	popupType = PopupType::NONE;
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

struct TextLine {
	std::string_view text;
	size_t pixel_width{0};
};

constexpr std::size_t kMaxNumLines = 8;
struct TextLineBreakdown {
	etl::vector<TextLine, kMaxNumLines> lines;
	size_t max_width_per_line;

	[[nodiscard]] size_t maxPixelWidth() const {
		return std::ranges::max_element(lines, {}, &TextLine::pixel_width)->pixel_width;
	}

	TextLineBreakdown(std::string_view text, size_t char_height, size_t max_pixels_per_line);
};

/// @brief get the relevant width and spacing for a character
/// @param c the character to get the width and spacing for
/// @param height the desired height of the character in pixels
/// @return a pair containing:
/// * updated line width in pixels
/// * number of spacing in pixels added after the last character
std::pair<size_t, size_t> getWidthPixels(char c, size_t height) {
	return {deluge::hid::display::OLED::popup.getCharWidthInPixels(c, height),
	        deluge::hid::display::OLED::popup.getCharSpacingInPixels(c, height, false)};
}

/// @brief get the total width of a string in pixels
/// @param text the string to get the width for
/// @param height the desired height of the font in pixels
/// @return the total width of the string in pixels
size_t getWidthPixels(std::string_view text, size_t height) {
	size_t total_width = 0;
	size_t index = 0;
	auto l = text.length();
	for (char c : text) {
		auto [char_width, char_spacing] = getWidthPixels(c, height);
		if (index++ == l - 1) {
			total_width += char_width;
		}
		else {
			total_width += char_spacing + char_width;
		}
	}

	return total_width;
}

/// Take a string and break it down into lines that fit within a given width
/// @param text the string to be broken down
/// @param text_height the height of the text in pixels
/// @param max_pixels_per_line the maximum width of a line in pixels
/// This function iterates through the string character by character, calculating the width of each character and the
/// spacing after it. If adding a character would exceed the maximum width, it looks for the first word break character
/// (space or underscore) to break the line. If no word break character is found, it breaks at the end of the string.
/// The function also handles newline characters by breaking the line at that point. The resulting lines are stored in
/// the `lines` vector and their pixel widths are calculated. The function also keeps track of the maximum width of any
/// line for later use.
/// @note The function assumes that the input string is valid and does not contain any invalid characters.
TextLineBreakdown::TextLineBreakdown(std::string_view text, size_t char_height, size_t max_pixels_per_line)
    : max_width_per_line{max_pixels_per_line} {
	size_t line_width = 0;
	size_t last_char_spacing = 0;

	for (const char* c = text.begin(); c != text.end(); ++c) {
		// If we hit a newline character, we need to break the line
		if (*c == '\n') {
			// get the index of the newline character
			size_t end_idx = std::distance(text.begin(), c);

			// add the current line to the lines vector
			auto t = text.substr(0, end_idx);
			auto l = getWidthPixels(t, char_height);
			if (l > max_pixels_per_line) {
				FREEZE_WITH_ERROR("P002");
			}
			lines.push_back({
			    .text = t,
			    .pixel_width = l,
			});

			// remove the text up to and including the newline
			text.remove_prefix(end_idx + 1);

			// reset the current line width
			line_width = 0;

			// reset the iterator to the start of the new text
			c = text.begin();
			continue;
		}

		auto [char_width, char_spacing] = getWidthPixels(*c, char_height);

		// If the new character would exceed the max width of a line, we need to break the line
		if (line_width + char_width > max_width_per_line) {

			// search for the latest word break character (space or underscore)
			size_t end_idx = text.find_last_of(" _", std::distance(text.begin(), c));

			// get the text up to the word break character
			std::string_view text_upto_word_start = text.substr(0, end_idx);

			// add the line to the lines vector
			lines.push_back({
			    .text = text_upto_word_start,
			    .pixel_width = getWidthPixels(text_upto_word_start, char_height),
			});

			// remove the text up to the word break character
			text.remove_prefix(end_idx + 1);

			// reset the current line width
			line_width = 0;

			// reset the iterator to the start of the new text
			c = text.begin();

			continue;
		}

		line_width += char_width + char_spacing;
	}

	// Add the last line if there is any text left
	if (!text.empty()) {
		lines.push_back({
		    .text = text,
		    .pixel_width = getWidthPixels(text, char_height),
		});
	}
	if (maxPixelWidth() > max_pixels_per_line) {
		FREEZE_WITH_ERROR("p001");
	}
}

void OLED::drawPermanentPopupLookingText(std::string_view text) {
	constexpr int32_t double_margin = 12;

	TextLineBreakdown text_line_breakdown(text, kTextSpacingY, OLED_MAIN_WIDTH_PIXELS - double_margin);

	auto total_width = static_cast<int32_t>(text_line_breakdown.maxPixelWidth());
	auto total_height = static_cast<int32_t>(text_line_breakdown.lines.size() * kTextSpacingY);

	int32_t min_x_pixel = (OLED_MAIN_WIDTH_PIXELS - total_width - double_margin) / 2;
	int32_t max_x_pixel = OLED_MAIN_WIDTH_PIXELS - min_x_pixel;

	int32_t min_y_pixel = (OLED_MAIN_HEIGHT_PIXELS - total_height - double_margin) / 2;
	int32_t max_y_pixel = OLED_MAIN_HEIGHT_PIXELS - min_y_pixel;

	main.drawRectangle(min_x_pixel, min_y_pixel, max_x_pixel, max_y_pixel);

	int32_t text_pixel_y = std::max<int32_t>((OLED_MAIN_HEIGHT_PIXELS - total_height) / 2, 0);

	for (TextLine& line : text_line_breakdown.lines) {
		int32_t text_pixel_x = (OLED_MAIN_WIDTH_PIXELS - line.pixel_width) >> 1;
		main.drawString(line.text, text_pixel_x, text_pixel_y, kTextSpacingX, kTextSpacingY);
		text_pixel_y += kTextSpacingY;
	}

	drawnPermanentPopup = true;
}

void OLED::popupText(std::string_view text, bool persistent, PopupType type) {
	constexpr int32_t double_margin = 12;
	TextLineBreakdown breakdown(text, kTextSpacingY, OLED_MAIN_WIDTH_PIXELS - double_margin);

	auto total_width = static_cast<int32_t>(breakdown.maxPixelWidth());
	auto total_height = static_cast<int32_t>(breakdown.lines.size() * kTextSpacingY);

	setupPopup(type, total_width + double_margin, total_height + double_margin);

	int32_t text_pixel_y = std::max<int32_t>((OLED_MAIN_HEIGHT_PIXELS - total_height) / 2, 0);

	for (TextLine& line : breakdown.lines) {
		if (text_pixel_y >= OLED_MAIN_HEIGHT_PIXELS) {
			continue;
		}
		int32_t text_pixel_x = (OLED_MAIN_WIDTH_PIXELS - static_cast<int32_t>(line.pixel_width)) / 2;
		popup.drawString(line.text, text_pixel_x, text_pixel_y, kTextSpacingX, kTextSpacingY);
		text_pixel_y += kTextSpacingY;
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

// Draws a cyclic animation while the Deluge is working on a loading or saving operation
void updateWorkingAnimation() {
	const int32_t w1 = 5;    // spacing between rectangles
	const int32_t w2 = 5;    // width of animated portion of rectangle
	const int32_t h = 8;     // height of rectangle
	int32_t offset = w2 - 2; // causes shifting lines to overlap by 2 pixels

	const int32_t animation_width = w1 + w2 * 2 + 2;
	const int32_t animation_height = h + 1;
	const int32_t popupX = OLED_MAIN_WIDTH_PIXELS - 1 - animation_width;
	const int32_t popupY = OLED_MAIN_TOPMOST_PIXEL + 2;

	if (!started_animation) { // initialize the animation
		started_animation = true;

		deluge::hid::display::OLED::setupPopup(PopupType::NOTIFICATION, animation_width, animation_height, popupX,
		                                       popupY);
	}

	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::popup;

	// Calculate positions using absolute coordinates (popup coordinates)
	const int32_t x_max = popupX + animation_width;
	const int32_t x_min = x_max - (w1 + w2 * 2); // this has to be out farther for the movement sequence
	const int32_t x2 = x_max - w2;               // starting position of right rectangle
	const int32_t y1 = popupY;                   // top of rectangles (absolute coordinates)
	const int32_t y2 = y1 + h - 1;               // bottom of rectangles
	int32_t h2;                                  // height of animated portion (will increase over time)
	// position of left side of starting stack that will be shifted over
	const int32_t x_pos2 = loading ? x2 - working_animation_count + 1 : x_min + 1 + working_animation_count;
	const int32_t t_reset = w1 + w2 + (h - 2) * offset;

	if (working_animation_count == 1) { // first frame after initialization
		// clear space and draw outer borders that will not change during the animation
		image.clearAreaExact(popupX, popupY, popupX + animation_width - 1, popupY + animation_height - 1);
		image.drawRectangle(x_min, y1, x_max, y2);
		image.clearAreaExact(x_min + w2 + 1, popupY, x_max - w2 - 1, popupY + animation_height - 1);
		h2 = h - 2; // will cause rectangle to be filled in at the start
	}
	else {
		h2 = std::min((working_animation_count + 2) / offset, h - 2);
	}

	// clears the area gradually on subsequent loops.
	image.clearAreaExact(x_min + 1, y1 + 1, x_max - 1, y1 + h2);
	if (!loading)
		offset *= -1;
	for (int i = 0; i < h2; i++) {
		int32_t x_pos = x_pos2 + i * offset; // Offsets positions which are later clamped. Causes staggered shifting.
		// backfilling lines from top to bottom
		if (loading && x_pos < x_min)
			image.drawHorizontalLine(y1 + 1 + i, x2, x_max - 1);
		if (!loading && x_pos > x2)
			image.drawHorizontalLine(y1 + 1 + i, x_min + 1, x_min + w2);
		x_pos = std::clamp(x_pos, x_min + 1, x2);
		// horizontally shifting lines
		image.drawHorizontalLine(y1 + 1 + i, x_pos, x_pos + w2 - 1);
	}

	if (working_animation_count == t_reset) {
		// completed animation, just have to add final backfill line.
		working_animation_count = 1;
		if (loading)
			image.drawHorizontalLine(y2 - 1, x2, x_max - 1);
		else
			image.drawHorizontalLine(y2 - 1, x_min + 1, x_min + w2);
		uiTimerManager.setTimer(TimerName::LOADING_ANIMATION, 350); // pause at end of animation cycle before restarting
	}
	else {
		if (working_animation_count == 1)
			uiTimerManager.setTimer(TimerName::LOADING_ANIMATION, 350); // delay the start of the animation sequence
		else
			uiTimerManager.setTimer(TimerName::LOADING_ANIMATION, 70); // time interval between animation steps
	}
}

void OLED::displayWorkingAnimation(std::string_view word) {
	loading = (word == "Loading");
	if (working_animation_count) {
		uiTimerManager.unsetTimer(TimerName::LOADING_ANIMATION);
	}
	working_animation_count = 1;
	started_animation = false;
	updateWorkingAnimation();
	markChanged();
}

void OLED::removeWorkingAnimation() {
	// return; // infinite animation duration for debugging purposes
	if (hasPopupOfType(PopupType::NOTIFICATION)) {
		removePopup();
	}
	if (working_animation_count) {
		uiTimerManager.unsetTimer(TimerName::LOADING_ANIMATION);
		working_animation_count = 0;
	}
}

void OLED::displayNotification(std::string_view param_title, std::optional<std::string_view> param_value) {
	DEF_STACK_STRING_BUF(titleBuf, 25);
	titleBuf.append(param_title);

	constexpr uint8_t start_x = 0;
	constexpr uint8_t end_x = OLED_MAIN_WIDTH_PIXELS - 1;
	constexpr uint8_t start_y = OLED_MAIN_TOPMOST_PIXEL;
	constexpr uint8_t end_y = OLED_MAIN_TOPMOST_PIXEL + kTextSpacingY + 1;
	constexpr uint8_t width = end_x - start_x + 1;
	constexpr uint8_t height = end_y - start_y;
	constexpr int32_t padding_left = 4;

	int32_t title_width = popup.getStringWidthInPixels(param_title.data(), kTextSpacingY);
	const int32_t value_width =
	    param_value.has_value() ? popup.getStringWidthInPixels(param_value.value().data(), kTextSpacingY) : 0;

	if (value_width > 0) {
		// Truncate the title string until we have space to display the value
		while (title_width + padding_left + value_width > OLED_MAIN_WIDTH_PIXELS - 7) {
			titleBuf.truncate(titleBuf.size() - 1);
			title_width = popup.getStringWidthInPixels(titleBuf.data(), kTextSpacingY);
		}
	}

	setupPopup(PopupType::NOTIFICATION, width - 1, height, start_x, start_y);

	// Draw the title and value
	const bool no_inversion = FlashStorage::accessibilityMenuHighlighting == MenuHighlighting::NO_INVERSION;
	constexpr uint8_t title_start_x = padding_left;
	const uint8_t title_start_y = no_inversion ? start_y : start_y + 1;
	popup.drawString(titleBuf.data(), title_start_x, title_start_y, kTextSpacingX, kTextSpacingY);

	if (value_width > 0) {
		popup.drawChar(':', title_start_x + title_width, title_start_y, kTextSpacingX, kTextSpacingY);
		popup.drawString(param_value.value().data(), title_start_x + title_width + 8, title_start_y, kTextSpacingX,
		                 kTextSpacingY);
	}

	if (no_inversion) {
		for (uint8_t x = start_x + 1; x < end_x; x += 2) {
			popup.drawPixel(x, end_y);
		}
	}
	else {
		popup.invertAreaRounded(start_x, width, start_y, end_y);
		popup.drawPixel(start_x, start_y);
		popup.drawPixel(end_x, start_y);
	}

	markChanged();
	uiTimerManager.setTimer(TimerName::DISPLAY, 2000);
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

void OLED::consoleText(std::string_view text) {
	constexpr int32_t textPixelX = 8;

	int32_t char_width_pixels = 6;
	int32_t char_height_pixels = 7;

	TextLineBreakdown breakdown(text, char_height_pixels, OLED_MAIN_WIDTH_PIXELS - textPixelX);

	int32_t text_pixel_y = setupConsole((breakdown.lines.size() * char_height_pixels) + 1) + 1;

	for (TextLine& line : breakdown.lines) {
		console.drawString(line.text, textPixelX, text_pixel_y, char_width_pixels, char_height_pixels);
		text_pixel_y += char_height_pixels;
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
	String string_;
};

#define NUM_SIDE_SCROLLERS 2

SideScroller sideScrollers[NUM_SIDE_SCROLLERS];
// text will be copied into the scroller, caller does not need to keep it allocated
void OLED::setupSideScroller(int32_t index, std::string_view text, int32_t startX, int32_t endX, int32_t startY,
                             int32_t endY, int32_t textSpacingX, int32_t textSizeY, bool doHighlight) {

	SideScroller* scroller = &sideScrollers[index];
	scroller->textLength = text.size();

	scroller->stringLengthPixels = 0;

	int32_t charIdx = 0;
	for (char const c : text) {
		int32_t char_spacing_pixels = main.getCharSpacingInPixels(c, textSizeY, charIdx == scroller->textLength);
		int32_t charWidth = main.getCharWidthInPixels(c, textSizeY) + char_spacing_pixels;
		scroller->stringLengthPixels += charWidth;
		charIdx++;
	}

	scroller->boxLengthPixels = endX - startX;
	if (scroller->stringLengthPixels <= scroller->boxLengthPixels) {
		return;
	}

	scroller->string_.set(text.data(), static_cast<int32_t>(text.size()));
	scroller->text = scroller->string_.get();
	scroller->pos = 0;
	scroller->startX = startX;
	scroller->endX = endX;
	scroller->startY = startY;
	scroller->endY = endY;
	scroller->textSpacingX = scroller->stringLengthPixels / scroller->textLength;
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
			scroller->string_.clear();
			scroller->text = nullptr;
		}
		uiTimerManager.unsetTimer(TimerName::OLED_SCROLLING_AND_BLINKING);
	}
}

void OLED::timerRoutine() {
	if (working_animation_count) {
		working_animation_count++;
		updateWorkingAnimation();
		markChanged();
	}
	else {
		removePopup(); // for the regular display popups
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

	bool doScroll = !playbackHandler.isEitherClockActive(); // only scroll when playback is off
	bool finished = true;

	for (int32_t s = 0; s < NUM_SIDE_SCROLLERS; s++) {
		bool doRender = true;
		SideScroller* scroller = &sideScrollers[s];
		if (scroller->text) {
			if (doScroll) {
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
			}
			// if playback is running, don't scroll
			else {
				// re-render if we're not at the beginning
				doRender = (scroller->pos > 0);
				// reset scroll position to beginning
				scroller->pos = 0;
			}

			if (doRender) {
				int32_t endX = scroller->endX;
				bool doInversion = FlashStorage::accessibilityMenuHighlighting != MenuHighlighting::NO_INVERSION;
				if (!doInversion) {
					// for submenu's, this is the padding before the icon's are rendered
					// need to clear this area otherwise it leaves a white pixels
					endX += 4;
				}
				// Ok, have to render.
				main.clearAreaExact(scroller->startX, scroller->startY, endX - 1, scroller->endY);
				main.drawString(scroller->text, scroller->startX, scroller->startY, scroller->textSpacingX,
				                scroller->textSizeY, scroller->pos, scroller->endX);
				if (scroller->doHighlight && doInversion) {
					main.invertArea(scroller->startX, scroller->endX - scroller->startX, scroller->startY,
					                scroller->endY);
				}
			}
		}
	}

	markChanged();
	// Workaround for glitches during scrolling. Not entirely obvious _why_
	// it glitches, though. Some sort of timing issue between doAnyPendingUIRendering,
	// uiTimerManager.routine() ?
	sendMainImage();

	int32_t timeInterval;
	if (!finished) {
		timeInterval = (sideScrollerDirection >= 0) ? 15 : 5;
	}
	else {
		timeInterval = kScrollTime;
		if (doScroll) {
			sideScrollerDirection = -sideScrollerDirection;
		}
		// if  we're not scrolling, we reset the scroll position to forward
		else {
			sideScrollerDirection = 1;
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

	// Or if it hasn't timed out, then provided we didn't already want a real-soon callback, come back when it does
	// time out
	else if (!timeTilNext) {
		timeTilNext = timeLeft;
	}

	if (timeTilNext) {
		uiTimerManager.setTimerSamples(TimerName::OLED_CONSOLE, timeTilNext);
	}

	markChanged();
}

void OLED::freezeWithError(std::string_view text) {
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
