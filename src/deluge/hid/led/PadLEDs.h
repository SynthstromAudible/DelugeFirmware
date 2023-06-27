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

#ifndef PADLEDS_H_
#define PADLEDS_H_

#include "r_typedefs.h"
#include "definitions.h"

#define FLASH_CURSOR_FAST 0
#define FLASH_CURSOR_OFF 1
#define FLASH_CURSOR_SLOW 2

class AudioClip;

namespace PadLEDs {
extern uint8_t image[displayHeight][displayWidth + sideBarWidth][3];               // 255 = full brightness
extern uint8_t occupancyMask[displayHeight][displayWidth + sideBarWidth];          // 64 = full occupancy
extern uint8_t imageStore[displayHeight * 2][displayWidth + sideBarWidth][3];      // 255 = full brightness
extern uint8_t occupancyMaskStore[displayHeight * 2][displayWidth + sideBarWidth]; // 64 = full occupancy

extern bool transitionTakingPlaceOnRow[displayHeight];

extern int explodeAnimationYOriginBig;
extern int explodeAnimationXStartBig;
extern int explodeAnimationXWidthBig;

extern int8_t animationDirection;
extern bool renderingLock;
extern uint8_t flashCursor;

extern int16_t animatedRowGoingTo[];
extern int16_t animatedRowGoingFrom[];
extern uint8_t numAnimatedRows;

extern int zoomPinSquare[displayHeight];
extern bool zoomingIn;
extern int8_t zoomMagnitude;

void init();
void sortLedsForCol(int x);
void writeToSideBar(uint8_t sideBarX, uint8_t yDisplay, uint8_t red, uint8_t green, uint8_t blue);
void renderInstrumentClipCollapseAnimation(int xStart, int xEnd, int progress);
void renderClipExpandOrCollapse();
void renderNoteRowExpandOrCollapse();
void clearAllPadsWithoutSending();
void clearMainPadsWithoutSending();

void sendOutMainPadColours();
void sendOutMainPadColoursSoon();
void sendOutSidebarColours();
void sendOutSidebarColoursSoon();

void skipGreyoutFade();
void reassessGreyout(bool doInstantly = false);
void doGreyoutInstantly();

void renderZoom();
void renderZoomWithProgress(int inImageTimesBiggerThanNative, uint32_t inImageFadeAmount, uint8_t* innerImage,
                            uint8_t* outerImage, int innerImageLeftEdge, int outerImageLeftEdge,
                            int innerImageRightEdge, int outerImageRightEdge, int innerImageTotalWidth,
                            int outerImageTotalWidth);
void renderScroll();
void setupScroll(int8_t thisScrollDirection, uint8_t thisAreaToScroll, bool scrollIntoNothing = false,
                 int numSquaresToScroll = displayWidth);

void sendRGBForOnePadFast(int x, int y, const uint8_t* colourSource);
void clearTickSquares(bool shouldSend = true);
void setTickSquares(const uint8_t* squares, const uint8_t* colours);
void renderExplodeAnimation(int explodedness, bool shouldSendOut = true);
void renderAudioClipExplodeAnimation(int explodedness, bool shouldSendOut = true);
void clearSideBar();
void renderFade(int progress);
void recordTransitionBegin(unsigned int newTransitionLength);
int getTransitionProgress();
void renderAudioClipExpandOrCollapse();
void setupInstrumentClipCollapseAnimation(bool collapsingOutOfClipMinder);
void setupAudioClipCollapseOrExplodeAnimation(AudioClip* clip);

void setGreyoutAmount(float newAmount);

inline void sendRGBForOneCol(int x);
void setTimerForSoon();
void renderZoomedSquare(int32_t outputSquareStartOnOutImage, int32_t outputSquareEndOnOutImage,
                        uint32_t outImageTimesBigerThanNormal, unsigned int sourceImageFade, uint32_t* output,
                        uint8_t* inputImageRow, int inputImageWidth, bool* drawingAnything);
bool shouldNotRenderDuringTimerRoutine();
void renderAudioClipCollapseAnimation(int progress);

void timerRoutine();
void copyBetweenImageStores(uint8_t* dest, uint8_t* source, int destWidth, int sourceWidth, int copyWidth);
void moveBetweenImageStores(uint8_t* dest, uint8_t* source, int destWidth, int sourceWidth, int copyWidth);

} // namespace PadLEDs

#endif /* PADLEDS_H_ */
