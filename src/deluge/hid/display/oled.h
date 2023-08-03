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

#pragma once

#ifdef __cplusplus
#include "definitions_cxx.hpp"
#include <string>

namespace OLED {

void drawOnePixel(int32_t x, int32_t y);
void clearMainImage();
void clearAreaExact(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY, uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);

void drawRectangle(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY, uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);
void drawVerticalLine(int32_t pixelX, int32_t startY, int32_t endY, uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);
void drawHorizontalLine(int32_t pixelY, int32_t startX, int32_t endX, uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);
void drawString(std::string_view, int32_t pixelX, int32_t pixelY, uint8_t* image, int32_t imageWidth, int32_t textWidth,
                int32_t textHeight, int32_t scrollPos = 0, int32_t endX = OLED_MAIN_WIDTH_PIXELS);
void drawStringFixedLength(char const* string, int32_t length, int32_t pixelX, int32_t pixelY, uint8_t* image,
                           int32_t imageWidth, int32_t textWidth, int32_t textHeight);
void drawStringCentred(char const* string, int32_t pixelY, uint8_t* image, int32_t imageWidth, int32_t textWidth,
                       int32_t textHeight, int32_t centrePos = (OLED_MAIN_WIDTH_PIXELS >> 1));
void drawStringCentredShrinkIfNecessary(char const* string, int32_t pixelY, uint8_t* image, int32_t imageWidth,
                                        int32_t textWidth, int32_t textHeight);
void drawStringAlignRight(char const* string, int32_t pixelY, uint8_t* image, int32_t imageWidth, int32_t textWidth,
                          int32_t textHeight, int32_t rightPos = OLED_MAIN_WIDTH_PIXELS);
void drawChar(uint8_t theChar, int32_t pixelX, int32_t pixelY, uint8_t* image, int32_t imageWidth, int32_t textWidth,
              int32_t textHeight, int32_t scrollPos = 0, int32_t endX = OLED_MAIN_WIDTH_PIXELS);
void drawGraphicMultiLine(uint8_t const* graphic, int32_t startX, int32_t startY, int32_t width, uint8_t* image,
                          int32_t height = 8, int32_t numBytesTall = 1);
void drawScreenTitle(std::string_view text);

void setupBlink(int32_t minX, int32_t width, int32_t minY, int32_t maxY, bool shouldBlinkImmediately);
void stopBlink();

void invertArea(int32_t xMin, int32_t width, int32_t startY, int32_t endY, uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);

void sendMainImage();

void setupPopup(int32_t width, int32_t height);
void removePopup();
void popupText(char const* text, bool persistent = false);
bool isPopupPresent();

void displayWorkingAnimation(char const* word);
void removeWorkingAnimation();

void timerRoutine();

void setupConsole(int32_t width, int32_t height);
void consoleText(char const* text);

void stopScrollingAnimation();
void setupSideScroller(int32_t index, char const* text, int32_t startX, int32_t endX, int32_t startY, int32_t endY,
                       int32_t textSpacingX, int32_t textSizeY, bool doHilight);
void drawPermanentPopupLookingText(char const* text);

void freezeWithError(char const* text);
void consoleTimerEvent();
void scrollingAndBlinkingTimerEvent();

extern uint8_t oledMainImage[OLED_MAIN_HEIGHT_PIXELS >> 3][OLED_MAIN_WIDTH_PIXELS];
extern uint8_t oledMainPopupImage[OLED_MAIN_HEIGHT_PIXELS >> 3][OLED_MAIN_WIDTH_PIXELS];
extern uint8_t oledMainConsoleImage[kConsoleImageNumRows][OLED_MAIN_WIDTH_PIXELS];

// pointer to one of the three above (the one currently displayed)
extern uint8_t (*oledCurrentImage)[OLED_MAIN_WIDTH_PIXELS];

extern const uint8_t folderIcon[];
extern const uint8_t waveIcon[];
extern const uint8_t songIcon[];
extern const uint8_t synthIcon[];
extern const uint8_t kitIcon[];
extern const uint8_t downArrowIcon[];
extern const uint8_t rightArrowIcon[];

} // namespace OLED
#endif
