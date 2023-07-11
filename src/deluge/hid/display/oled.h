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

#include "definitions.h"

#if HAVE_OLED
#ifdef __cplusplus

namespace OLED {

void mainPutText(char const* text);
void drawOnePixel(int x, int y);
void clearMainImage();
void clearAreaExact(int minX, int minY, int maxX, int maxY, uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);

void drawRectangle(int minX, int minY, int maxX, int maxY, uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);
void drawVerticalLine(int pixelX, int startY, int endY, uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);
void drawHorizontalLine(int pixelY, int startX, int endX, uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);
void drawString(char const* string, int pixelX, int pixelY, uint8_t* image, int imageWidth, int textWidth,
                int textHeight, int scrollPos = 0, int endX = OLED_MAIN_WIDTH_PIXELS);
void drawStringFixedLength(char const* string, int length, int pixelX, int pixelY, uint8_t* image, int imageWidth,
                           int textWidth, int textHeight);
void drawStringCentred(char const* string, int pixelY, uint8_t* image, int imageWidth, int textWidth, int textHeight,
                       int centrePos = (OLED_MAIN_WIDTH_PIXELS >> 1));
void drawStringCentredShrinkIfNecessary(char const* string, int pixelY, uint8_t* image, int imageWidth, int textWidth,
                                        int textHeight);
void drawStringAlignRight(char const* string, int pixelY, uint8_t* image, int imageWidth, int textWidth, int textHeight,
                          int rightPos = OLED_MAIN_WIDTH_PIXELS);
void drawChar(uint8_t theChar, int pixelX, int pixelY, uint8_t* image, int imageWidth, int textWidth, int textHeight,
              int scrollPos = 0, int endX = OLED_MAIN_WIDTH_PIXELS);
void drawGraphicMultiLine(uint8_t const* graphic, int startX, int startY, int width, uint8_t* image, int height = 8,
                          int numBytesTall = 1);
void drawScreenTitle(char const* title);

void setupBlink(int minX, int width, int minY, int maxY, bool shouldBlinkImmediately);
void stopBlink();

void invertArea(int xMin, int width, int startY, int endY, uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);

void sendMainImage();

void setupPopup(int width, int height);
void removePopup();
void popupText(char const* text, bool persistent = false);
bool isPopupPresent();

void displayWorkingAnimation(char const* word);
void removeWorkingAnimation();

void timerRoutine();

void setupConsole(int width, int height);
void consoleText(char const* text);

void stopScrollingAnimation();
void setupSideScroller(int index, char const* text, int startX, int endX, int startY, int endY, int textSpacingX,
                       int textSizeY, bool doHilight);
void drawPermanentPopupLookingText(char const* text);

void freezeWithError(char const* text);
void consoleTimerEvent();
void scrollingAndBlinkingTimerEvent();

extern uint8_t oledMainImage[OLED_MAIN_HEIGHT_PIXELS >> 3][OLED_MAIN_WIDTH_PIXELS];
extern uint8_t oledMainPopupImage[OLED_MAIN_HEIGHT_PIXELS >> 3][OLED_MAIN_WIDTH_PIXELS];
extern uint8_t oledMainConsoleImage[CONSOLE_IMAGE_NUM_ROWS][OLED_MAIN_WIDTH_PIXELS];

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

extern "C" {
#endif

void consoleTextIfAllBootedUp(char const* text);

#ifdef __cplusplus
}
#endif

#endif /* HAVE_OLED */
