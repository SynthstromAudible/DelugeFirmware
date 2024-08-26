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

#pragma once

#include "definitions_cxx.hpp"
#include "drivers/pic/pic.h"
#include <cstdint>

#define FLASH_CURSOR_FAST 0
#define FLASH_CURSOR_OFF 1
#define FLASH_CURSOR_SLOW 2

extern "C" {
#include "RZA1/uart/sio_char.h"
}

class UI;
class AudioClip;

namespace PadLEDs {
extern RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth];                      // 255 = full brightness
extern uint8_t occupancyMask[kDisplayHeight][kDisplayWidth + kSideBarWidth];          // 64 = full occupancy
extern RGB imageStore[kDisplayHeight * 2][kDisplayWidth + kSideBarWidth];             // 255 = full brightness
extern uint8_t occupancyMaskStore[kDisplayHeight * 2][kDisplayWidth + kSideBarWidth]; // 64 = full occupancy

extern bool transitionTakingPlaceOnRow[kDisplayHeight];

extern int32_t explodeAnimationYOriginBig;
extern int32_t explodeAnimationXStartBig;
extern int32_t explodeAnimationXWidthBig;

extern int8_t explodeAnimationDirection;
extern UI* explodeAnimationTargetUI;
extern bool renderingLock;
extern uint8_t flashCursor;

extern int16_t animatedRowGoingTo[];
extern int16_t animatedRowGoingFrom[];
extern uint8_t numAnimatedRows;

extern int32_t zoomPinSquare[kDisplayHeight];
extern bool zoomingIn;
extern int8_t zoomMagnitude;

void init();
void sortLedsForCol(int32_t x);
void set(Cartesian pad, RGB colour);
void renderInstrumentClipCollapseAnimation(int32_t xStart, int32_t xEnd, int32_t progress);
void renderClipExpandOrCollapse();
void renderNoteRowExpandOrCollapse();
void clearAllPadsWithoutSending();
void clearMainPadsWithoutSending();
void clearColumnWithoutSending(int32_t x);

void sendOutMainPadColours();
void sendOutMainPadColoursSoon();
void sendOutSidebarColours();
void sendOutSidebarColoursSoon();

void skipGreyoutFade();
void reassessGreyout(bool doInstantly = false);
void doGreyoutInstantly();

void setRefreshTime(int32_t newTime);
void changeRefreshTime(int32_t offset);
void changeDimmerInterval(int32_t offset);
void setDimmerInterval(int32_t newInterval);
void setBrightnessLevel(uint8_t level);
void renderZoom();
void renderZoomWithProgress(int32_t inImageTimesBiggerThanNative, uint32_t inImageFadeAmount, uint8_t* innerImage,
                            uint8_t* outerImage, int32_t innerImageLeftEdge, int32_t outerImageLeftEdge,
                            int32_t innerImageRightEdge, int32_t outerImageRightEdge, int32_t innerImageTotalWidth,
                            int32_t outerImageTotalWidth);

namespace horizontal {
void setupScroll(int8_t thisScrollDirection, uint8_t thisAreaToScroll, bool scrollIntoNothing = false,
                 int32_t numSquaresToScroll = kDisplayWidth);
void renderScroll();
} // namespace horizontal

namespace vertical {
void setupScroll(int8_t thisScrollDirection, bool scrollIntoNothing = false);
void renderScroll();

extern uint8_t squaresScrolled;
extern int8_t scrollDirection;
extern bool scrollingToNothing;
} // namespace vertical

RGB prepareColour(int32_t x, int32_t y, RGB colourSource);
void clearTickSquares(bool shouldSend = true);
void setTickSquares(const uint8_t* squares, const uint8_t* colours);
void renderExplodeAnimation(int32_t explodedness, bool shouldSendOut = true);
void renderAudioClipExplodeAnimation(int32_t explodedness, bool shouldSendOut = true);
void clearSideBar();
void renderFade(int32_t progress);
void recordTransitionBegin(uint32_t newTransitionLength);
int32_t getTransitionProgress();
void renderAudioClipExpandOrCollapse();
void setupInstrumentClipCollapseAnimation(bool collapsingOutOfClipMinder);
void setupAudioClipCollapseOrExplodeAnimation(AudioClip* clip);

void setGreyoutAmount(float newAmount);

static inline void flashMainPad(int32_t x, int32_t y, int32_t colour = 0) {
	auto idx = y + (x * kDisplayHeight);
	if (colour > 0) {
		PIC::flashMainPadWithColourIdx(idx, colour);
		return;
	}
	PIC::flashMainPad(idx);
}

void setTimerForSoon();
void renderZoomedSquare(int32_t outputSquareStartOnOutImage, int32_t outputSquareEndOnOutImage,
                        uint32_t outImageTimesBigerThanNormal, uint32_t sourceImageFade, uint32_t* output,
                        uint8_t* inputImageRow, int32_t inputImageWidth, bool* drawingAnything);
bool shouldNotRenderDuringTimerRoutine();
void renderAudioClipCollapseAnimation(int32_t progress);

void timerRoutine();
void copyBetweenImageStores(uint8_t* dest, uint8_t* source, int32_t destWidth, int32_t sourceWidth, int32_t copyWidth);
void moveBetweenImageStores(uint8_t* dest, uint8_t* source, int32_t destWidth, int32_t sourceWidth, int32_t copyWidth);

} // namespace PadLEDs
