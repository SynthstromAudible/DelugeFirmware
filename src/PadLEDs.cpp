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

#include <AudioEngine.h>
#include "PadLEDs.h"
#include "WaveformRenderData.h"
#include <string.h>
#include "uitimermanager.h"
#include "MenuItemColour.h"
#include "song.h"
#include "SessionView.h"
#include "AudioClip.h"
#include "InstrumentClipView.h"
#include "WaveformRenderer.h"
#include "UI.h"
#include "ArrangerView.h"
#include "KeyboardScreen.h"
#include "AudioClipView.h"
#include "numericdriver.h"
#include "Sample.h"
#include "View.h"
#include "InstrumentClip.h"

extern "C" {
#include "sio_char.h"
}

namespace PadLEDs {
uint8_t image[displayHeight][displayWidth + sideBarWidth][3];               // 255 = full brightness
uint8_t occupancyMask[displayHeight][displayWidth + sideBarWidth];          // 64 = full occupancy
uint8_t imageStore[displayHeight * 2][displayWidth + sideBarWidth][3];      // 255 = full brightness
uint8_t occupancyMaskStore[displayHeight * 2][displayWidth + sideBarWidth]; // 64 = full occupancy

bool zoomingIn;
int8_t zoomMagnitude;
int zoomPinSquare[displayHeight];
bool transitionTakingPlaceOnRow[displayHeight];

uint8_t areaToScroll;
uint8_t squaresScrolled;
int8_t animationDirection;
bool scrollingIntoNothing; // Means we're scrolling into a black screen

int16_t animatedRowGoingTo[MAX_NUM_ANIMATED_ROWS];
int16_t animatedRowGoingFrom[MAX_NUM_ANIMATED_ROWS];
uint8_t numAnimatedRows;

int32_t greyProportion;
int8_t greyoutChangeDirection;
unsigned long greyoutChangeStartTime;

bool needToSendOutMainPadColours;
bool needToSendOutSidebarColours;

uint8_t flashCursor;

uint8_t slowFlashSquares[displayHeight];
uint8_t slowFlashColours[displayHeight];

int explodeAnimationYOriginBig;
int explodeAnimationXStartBig;
int explodeAnimationXWidthBig;

// We stash these here for during UI-transition animation, because if that's happening as part of an undo, the Sample might not be there anymore
int32_t sampleValueCentrePoint;
int32_t sampleValueSpan;
int32_t sampleMaxPeakFromZero;
WaveformRenderData waveformRenderData;
uint8_t audioClipColour[3];
bool sampleReversed;

// Same for InstrumentClips
int32_t clipLength;
uint8_t clipMuteSquareColour[3];

bool renderingLock;

uint32_t transitionLength;
uint32_t transitionStartTime;

uint32_t greyoutCols;
uint32_t greyoutRows;

void init() {
	memset(slowFlashSquares, 255, sizeof(slowFlashSquares));
}

bool shouldNotRenderDuringTimerRoutine() {
	return (renderingLock || currentUIMode == UI_MODE_EXPLODE_ANIMATION || currentUIMode == UI_MODE_ANIMATION_FADE
	        || currentUIMode == UI_MODE_HORIZONTAL_ZOOM || currentUIMode == UI_MODE_HORIZONTAL_SCROLL
	        || currentUIMode == UI_MODE_INSTRUMENT_CLIP_EXPANDING || currentUIMode == UI_MODE_INSTRUMENT_CLIP_COLLAPSING
	        || currentUIMode == UI_MODE_NOTEROWS_EXPANDING_OR_COLLAPSING);
}

void clearTickSquares(bool shouldSend) {

	unsigned int colsToSend = 0;

	if (flashCursor == FLASH_CURSOR_SLOW && !shouldNotRenderDuringTimerRoutine()) {
		for (int y = 0; y < displayHeight; y++) {

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
			if (slowFlashSquares[y] != 255) colsToSend |= (1 << slowFlashSquares[y]);
#else
			if (slowFlashSquares[y] != 255) colsToSend |= (1 << (slowFlashSquares[y] >> 1));
#endif
		}
	}

	memset(slowFlashSquares, 255, sizeof(slowFlashSquares));

	if (shouldSend && flashCursor == FLASH_CURSOR_SLOW && !shouldNotRenderDuringTimerRoutine()) {
		if (colsToSend) {
			for (int x = 0; x < 8; x++) {
				if (colsToSend & (1 << x)) {
					if (uartGetTxBufferSpace(UART_ITEM_PIC_PADS) <= NUM_BYTES_IN_COL_UPDATE_MESSAGE) break;

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
					sortLedsForCol(x);
#else
					sortLedsForCol(x << 1);
#endif
				}
			}
			uartFlushIfNotSending(UART_ITEM_PIC_PADS);
		}
	}
}

void setTickSquares(const uint8_t* squares, const uint8_t* colours) {

	unsigned int colsToSend = 0;

	if (flashCursor == FLASH_CURSOR_SLOW) {
		if (!shouldNotRenderDuringTimerRoutine()) {
			for (int y = 0; y < displayHeight; y++) {
				if (squares[y] != slowFlashSquares[y] || colours[y] != slowFlashColours[y]) {

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
					// Remember to update the new column
					if (squares[y] != 255) colsToSend |= (1 << squares[y]);

					// And the old column
					if (slowFlashSquares[y] != 255) colsToSend |= (1 << slowFlashSquares[y]);
#else
					// Remember to update the new column
					if (squares[y] != 255) colsToSend |= (1 << (squares[y] >> 1));

					// And the old column
					if (slowFlashSquares[y] != 255) colsToSend |= (1 << (slowFlashSquares[y] >> 1));
#endif
				}
			}
		}
	}
	else if (flashCursor == FLASH_CURSOR_FAST) {
		for (int y = 0; y < displayHeight; y++) {
			if (squares[y] != slowFlashSquares[y] && squares[y] != 255) {

				int colourMessage;
				if (colours[y] == 1) { // "Muted" colour
					colourMessage = 10;
					uint8_t mutedColour[3];
					mutedColourMenu.getRGB(mutedColour);
					for (int c = 0; c < 3; c++) {
						if (mutedColour[c] >= 64) colourMessage += (1 << c);
					}
sendColourMessage:
					bufferPICPadsUart(colourMessage);
				}
				else if (colours[y] == 2) { // Red
					colourMessage = 10 + 0b00000001;
					goto sendColourMessage;
				}

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
				bufferPICPadsUart(78 + squares[y] + y * displayWidth);
#else
				bufferPICPadsUart(24 + y + (squares[y] * displayHeight));
#endif
			}
		}
	}

	memcpy(slowFlashSquares, squares, displayHeight);
	memcpy(slowFlashColours, colours, displayHeight);

	if (flashCursor == FLASH_CURSOR_SLOW && !shouldNotRenderDuringTimerRoutine()) {
		// Actually send everything, if there was a change
		if (colsToSend) {
			for (int x = 0; x < 8; x++) {
				if (colsToSend & (1 << x)) {
					if (uartGetTxBufferSpace(UART_ITEM_PIC_PADS) <= NUM_BYTES_IN_COL_UPDATE_MESSAGE) break;
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
					sortLedsForCol(x);
#else
					sortLedsForCol(x << 1);
#endif
				}
			}
			uartFlushIfNotSending(UART_ITEM_PIC_PADS);
		}
	}
}

void clearAllPadsWithoutSending() {
	memset(image, 0, sizeof(image));
}

void clearMainPadsWithoutSending() {
	for (int y = 0; y < displayHeight; y++) {
		memset(image[y], 0, displayWidth * 3);
	}
}

void clearSideBar() {
	for (int y = 0; y < displayHeight; y++) {
		memset(&image[y][displayWidth], 0, 6);
	}

	sendOutSidebarColours();
}

// You'll want to call uartFlushToPICIfNotSending() after this
void sortLedsForCol(int x) {
	AudioEngine::logAction("MatrixDriver::sortLedsForCol");

#if DELUGE_MODEL >= DELUGE_MODEL_144_PAD
	x &= 0b11111110;
	bufferPICPadsUart((x >> 1) + 1);
	sendRGBForOneCol(x);
	sendRGBForOneCol(x + 1);
#else
	bufferPICPadsUart(x);
	sendRGBForOneCol(x);
#endif
}

inline void sendRGBForOneCol(int x) {
	for (int y = 0; y < displayHeight; y++) {
		sendRGBForOnePadFast(x, y, image[y][x]);
	}
}

const uint8_t flashColours[3][3] = {
    {130, 120, 130},
    {mutedColourRed, mutedColourGreen, mutedColourBlue}, // Not used anymore
    {255, 0, 0},
};

void sendRGBForOnePadFast(int x, int y, const uint8_t* colourSource) {

	uint8_t temp[3];

	if (flashCursor == FLASH_CURSOR_SLOW && slowFlashSquares[y] == x && currentUIMode != UI_MODE_HORIZONTAL_SCROLL) {
		if (slowFlashColours[y] == 1) { // If it's to be the "muted" colour, get that
			mutedColourMenu.getRGB(temp);
			colourSource = temp;
		}
		else { // Otherwise, pull from a referenced table line
			colourSource = flashColours[slowFlashColours[y]];
		}
	}

	if ((greyoutRows || greyoutCols)
	    && ((greyoutRows & (1 << y)) || (greyoutCols & (1 << (displayWidth + sideBarWidth - 1 - x))))) {
		uint8_t greyedOutColour[3];
		greyColourOut(colourSource, greyedOutColour, greyProportion);
		bufferPICPadsUart(greyedOutColour[0]);
		bufferPICPadsUart(greyedOutColour[1]);
		bufferPICPadsUart(greyedOutColour[2]);
	}

	else {
		bufferPICPadsUart(colourSource[0]);
		bufferPICPadsUart(colourSource[1]);
		bufferPICPadsUart(colourSource[2]);
	}
}

void writeToSideBar(uint8_t sideBarX, uint8_t yDisplay, uint8_t red, uint8_t green, uint8_t blue) {
	image[yDisplay][sideBarX + displayWidth][0] = red;
	image[yDisplay][sideBarX + displayWidth][1] = green;
	image[yDisplay][sideBarX + displayWidth][2] = blue;
}

void setupInstrumentClipCollapseAnimation(bool collapsingOutOfClipMinder) {
	clipLength = currentSong->currentClip->loopLength;

	if (collapsingOutOfClipMinder) {
		view.getClipMuteSquareColour(currentSong->currentClip,
		                             clipMuteSquareColour); // This shouldn't have to be done every time
	}
}

void renderInstrumentClipCollapseAnimation(int xStart, int xEndOverall, int progress) {
	AudioEngine::logAction("MatrixDriver::renderCollapseAnimation");

	memset(image, 0, sizeof(image));
	memset(occupancyMask, 0, sizeof(occupancyMask));

	if (!(isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING) || isUIModeActive(UI_MODE_INSTRUMENT_CLIP_EXPANDING))) {
		for (int row = 0; row < displayHeight; row++) {
			image[row][displayWidth][0] = enabledColourRed;
			image[row][displayWidth][1] = enabledColourGreen;
			image[row][displayWidth][2] = enabledColourBlue;
			occupancyMask[row][displayWidth] = 64;
		}
	}

	// Do some pre figuring out, which applies to all columns
	uint16_t* intensity1Array = (uint16_t*)miscStringBuffer;
	uint16_t* intensity2Array = (uint16_t*)&miscStringBuffer[MAX_NUM_ANIMATED_ROWS * sizeof(uint16_t)];
	int8_t newRowPosition1Array[MAX_NUM_ANIMATED_ROWS];

	for (int i = 0; i < numAnimatedRows; i++) {
		int newRowPosition = (int)animatedRowGoingFrom[i] * 65536
		                     + ((int)animatedRowGoingTo[i] - animatedRowGoingFrom[i]) * (65536 - progress);
		newRowPosition1Array[i] = newRowPosition >> 16;
		intensity2Array[i] = newRowPosition; // & 65535;
		intensity1Array[i] = 65535 - intensity2Array[i];
	}

	int greyStart =
	    instrumentClipView.getSquareFromPos(clipLength - 1, NULL, currentSong->xScroll[NAVIGATION_CLIP]) + 1;
	int xEnd = getMin(displayWidth, greyStart);

	int greyTop, greyBottom;

	if (currentUIMode == UI_MODE_NOTEROWS_EXPANDING_OR_COLLAPSING) {
		greyTop = displayHeight;
		greyBottom = 0;
	}

	else {
		greyTop = animatedRowGoingTo[0] + 1 + (((displayHeight - animatedRowGoingTo[0]) * progress + 32768) >> 16);
		greyBottom = animatedRowGoingTo[0] - (((animatedRowGoingTo[0]) * progress + 32768) >> 16);
		if (greyTop > displayHeight) greyTop = displayHeight;
		if (greyBottom < 0) greyBottom = 0;
	}

	if (xEnd < displayWidth) {

		if (xEnd < 0) xEnd = 0;

		for (int yDisplay = greyBottom; yDisplay < greyTop; yDisplay++) {
			memset(PadLEDs::image[yDisplay][xEnd], 7, (displayWidth - xEnd) * 3);
		}
	}

	for (int col = xStart; col < xEndOverall; col++) {

		if (col < displayWidth) {
			if (col >= xEnd) continue; // It's beyond the end of the Clip, and it's already been filled in grey

			// Or if it's greyed out cos of triplets...
			if (!instrumentClipView.isSquareDefined(col, currentSong->xScroll[NAVIGATION_CLIP])) {
				for (int yDisplay = greyBottom; yDisplay < greyTop; yDisplay++) {
					PadLEDs::image[yDisplay][col][0] = 7;
					PadLEDs::image[yDisplay][col][1] = 7;
					PadLEDs::image[yDisplay][col][2] = 7;
				}
				continue;
			}
		}

		for (int i = 0; i < numAnimatedRows; i++) {
			if (!occupancyMaskStore[i][col]) continue; // Nothing to do if there was nothing in this square

			uint8_t* squareColours = imageStore[i][col];

			uint8_t thisColour[3]; // Only used in some cases

			int intensity1 = intensity1Array[i];
			int intensity2 = intensity2Array[i];

			if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)
			    || isUIModeActive(UI_MODE_INSTRUMENT_CLIP_EXPANDING)) {

				// If the audition column, fade it out as we go
				if (col == displayWidth + sideBarWidth - 1) {
					intensity1 = ((uint32_t)intensity1 * progress) >> 16;
					intensity2 = ((uint32_t)intensity2 * progress) >> 16;
				}

				// If the mute-col, we want to alter the colour
				if (col == displayWidth) {

					for (int colour = 0; colour < 3; colour++) {
						int newColour = rshift_round((int)squareColours[colour] * progress, 16)
						                + rshift_round((int)clipMuteSquareColour[colour] * (65536 - progress), 16);
						thisColour[colour] = getMin(255, newColour);
					}
					squareColours = thisColour;
				}
			}

			if (newRowPosition1Array[i] >= 0 && newRowPosition1Array[i] < displayHeight) {
				drawSquare(squareColours, intensity1, PadLEDs::image[newRowPosition1Array[i]][col],
				           &occupancyMask[newRowPosition1Array[i]][col], occupancyMaskStore[i][col]);
			}

			if (newRowPosition1Array[i] >= -1 && newRowPosition1Array[i] < displayHeight - 1) {
				drawSquare(squareColours, intensity2, PadLEDs::image[newRowPosition1Array[i] + 1][col],
				           &occupancyMask[newRowPosition1Array[i] + 1][col], occupancyMaskStore[i][col]);
			}
		}
	}

	sendOutMainPadColours();
	sendOutSidebarColours();
}

void setupAudioClipCollapseOrExplodeAnimation(AudioClip* clip) {
	clipLength = clip->loopLength;
	clip->getColour(audioClipColour);

	sampleReversed = clip->sampleControls.reversed;

	Sample* sample = (Sample*)clip->sampleHolder.audioFile;

	if (ALPHA_OR_BETA_VERSION && !sample) numericDriver.freezeWithError("E311");

	sampleMaxPeakFromZero = sample->getMaxPeakFromZero();
	sampleValueCentrePoint = sample->getFoundValueCentrePoint();
	sampleValueSpan = sample->getValueSpan();

	waveformRenderData = clip->renderData;
}

void renderAudioClipCollapseAnimation(int progress) {
	memset(image, 0, sizeof(image));

	int endSquareDisplay = divide_round_negative(
	    clipLength - currentSong->xScroll[NAVIGATION_CLIP] - 1,
	    currentSong->xZoom[NAVIGATION_CLIP]); // Rounds it well down, so we get the "final square" kinda...
	int greyStart = endSquareDisplay + 1;
	int xEnd = getMin(displayWidth, greyStart);

	for (int col = 0; col < xEnd; col++) {
		waveformRenderer.renderOneColForCollapseAnimation(col, col, sampleMaxPeakFromZero, progress, PadLEDs::image,
		                                                  &waveformRenderData, audioClipColour, sampleReversed,
		                                                  sampleValueCentrePoint, sampleValueSpan);
	}

	if (xEnd < displayWidth) {

		if (xEnd < 0) xEnd = 0;

		int greyTop = waveformRenderer.collapseAnimationToWhichRow + 1
		              + (((displayHeight - waveformRenderer.collapseAnimationToWhichRow) * progress + 32768) >> 16);
		int greyBottom = waveformRenderer.collapseAnimationToWhichRow
		                 - (((waveformRenderer.collapseAnimationToWhichRow) * progress + 32768) >> 16);

		if (greyTop > displayHeight) greyTop = displayHeight;
		if (greyBottom < 0) greyBottom = 0;

		for (int yDisplay = greyBottom; yDisplay < greyTop; yDisplay++) {
			memset(PadLEDs::image[yDisplay][xEnd], 7, (displayWidth - xEnd) * 3);
		}
	}

	// What about the sidebar, did I just not animate that?

	sendOutMainPadColours();
}

// 2^16 used in place of "1" in "big" arithmetic below
void renderAudioClipExplodeAnimation(int explodedness, bool shouldSendOut) {

	memset(PadLEDs::image, 0, sizeof(PadLEDs::image));
	memset(occupancyMask, 0, sizeof(occupancyMask));

	int startBigNow = (((int64_t)explodeAnimationXStartBig * (65536 - explodedness)) >> 16);
	int widthBigWhenExploded = (displayWidth << 16);
	int widthBigWhenNotExploded = explodeAnimationXWidthBig;
	int difference = widthBigWhenExploded - widthBigWhenNotExploded;
	int widthBigNow = widthBigWhenNotExploded + (((int64_t)difference * explodedness) >> 16);

	int inverseScale = ((uint64_t)widthBigWhenExploded << 16) / widthBigNow;

	int xSourceRightEdge;

	for (int xDestSquareRightEdge = 0; xDestSquareRightEdge <= displayWidth; xDestSquareRightEdge++) {

		int xSourceLeftEdge =
		    xSourceRightEdge; // What was the last square's right edge is now the current square's left edge

		// From here on, we talk about the right edge of the destination square
		int xDestBig = xDestSquareRightEdge << 16;
		int xDestBigRelativeToStart = xDestBig - startBigNow;

		int xSourceBigRelativeToStartNow = ((int64_t)xDestBigRelativeToStart * inverseScale) >> 16;
		int xSourceBig = xSourceBigRelativeToStartNow;
		xSourceRightEdge = xSourceBig >> 16;

		// For first iteration, we just wanted that value, to use next time - and we should get out now
		if (!xDestSquareRightEdge) continue;

		if (xSourceRightEdge <= 0) continue; // <=0 probably looks a little bit better than <0

		// Ok, we need the max values between xSourceLeftEdge and xSourceRightEdge
		int xSourceLeftEdgeLimited = getMax(xSourceLeftEdge, 0);
		int xSourceRightEdgeLimited = getMin(xSourceRightEdge, displayWidth);

		int xDest = xDestSquareRightEdge - 1;
		waveformRenderer.renderOneColForCollapseAnimationZoomedOut(
		    xSourceLeftEdgeLimited, xSourceRightEdgeLimited, xDest, sampleMaxPeakFromZero, explodedness, PadLEDs::image,
		    &waveformRenderData, audioClipColour, sampleReversed, sampleValueCentrePoint, sampleValueSpan);

		if (xSourceRightEdge >= displayWidth)
			break; // If we got to the right edge of everything we want to draw onscreen
	}

	if (shouldSendOut) {
		sendOutMainPadColours();
		uiTimerManager.setTimer(TIMER_MATRIX_DRIVER, 35);
	}
}

// 2^16 used in place of "1" in "big" arithmetic below
void renderExplodeAnimation(int explodedness, bool shouldSendOut) {
	memset(image, 0, sizeof(image));
	memset(occupancyMask, 0, sizeof(occupancyMask));

	// Set up some stuff for each x-pos that we don't want to be constantly re-calculating
	int xDestArray[displayWidth];
	uint16_t xIntensityArray[displayWidth][2];

	int xStart = 0;
	int xEnd = displayWidth;

	for (int xSource = 0; xSource < displayWidth; xSource++) {
		int xSourceBig = xSource << 16;
		int xOriginBig = explodeAnimationXStartBig
		                 + (((int64_t)explodeAnimationXWidthBig * xSourceBig) >> (displayWidthMagnitude + 16));
		//xOriginBig = getMin(xOriginBig, explodeAnimationXStartBig + explodeAnimationXWidthBig - 65536);

		xOriginBig &= ~(
		    uint32_t)65535; // Make sure each pixel's "origin-point" is right on an exact square - rounded to the left. That'll match what we'll see in the arranger

		int xSourceBigRelativeToOrigin = xSourceBig - xOriginBig;
		int xDestBig = xOriginBig + (((int64_t)xSourceBigRelativeToOrigin * explodedness) >> 16);

		// Ok, so we're gonna squish this source square amongst 4 destination squares
		xDestArray[xSource] = xDestBig >> 16;

		// May as well narrow things down if we know now that some xSources won't end up onscreen
		if (xDestArray[xSource] < -1) {
			xStart = xSource + 1;
			continue;
		}
		else if (xDestArray[xSource] >= displayWidth) {
			xEnd = xSource;
			break;
		}
		xIntensityArray[xSource][1] = xDestBig; // & 65535;
		xIntensityArray[xSource][0] = 65535 - xIntensityArray[xSource][1];
	}

	for (int ySource = -1; ySource < displayHeight + 1; ySource++) {

		int ySourceBig = ySource << 16;
		int ySourceBigRelativeToOrigin = ySourceBig - explodeAnimationYOriginBig;
		int yDestBig = explodeAnimationYOriginBig + (((int64_t)ySourceBigRelativeToOrigin * explodedness) >> 16);
		int yDest = yDestBig >> 16;

		uint32_t yIntensity[2];
		yIntensity[1] = yDestBig & 65535;
		yIntensity[0] = 65535 - yIntensity[1];

		for (int xSource = xStart; xSource < xEnd; xSource++) {

			if (occupancyMaskStore[ySource + 1][xSource]) { // If there's actually anything in this source square...

				for (int xOffset = 0; xOffset < 2; xOffset++) {
					int xNow = xDestArray[xSource] + xOffset;
					if (xNow < 0) continue;
					if (xNow >= displayWidth) break;

					for (int yOffset = 0; yOffset < 2; yOffset++) {
						int yNow = yDest + yOffset;
						if (yNow < 0) continue;
						if (yNow >= displayHeight) break;

						uint32_t intensityNow = (yIntensity[yOffset] * xIntensityArray[xSource][xOffset]) >> 16;
						drawSquare(imageStore[ySource + 1][xSource], intensityNow, PadLEDs::image[yNow][xNow],
						           &occupancyMask[yNow][xNow], occupancyMaskStore[ySource + 1][xSource]);
					}
				}
			}
		}
	}

	if (shouldSendOut) {
		sendOutMainPadColours();
		uiTimerManager.setTimer(TIMER_MATRIX_DRIVER,
		                        35); // Nice small number of milliseconds here. This animation is prone to looking jerky
	}
}

void reassessGreyout(bool doInstantly) {
	uint32_t newCols, newRows;

	getUIGreyoutRowsAndCols(&newCols, &newRows);

	// If same as before, get out
	if (newCols == greyoutCols && newRows == greyoutRows) return;

	bool anythingBefore = (greyoutCols || greyoutRows);
	bool anythingNow = (newCols || newRows);

	bool anythingBoth = (anythingBefore && anythingNow);

	if (anythingNow) {
		greyoutCols = newCols;
		greyoutRows = newRows;
	}

	if (doInstantly || anythingBoth) {
		setGreyoutAmount(1);
		sendOutMainPadColoursSoon();
		sendOutSidebarColoursSoon();
	}
	else {
		greyoutChangeStartTime = AudioEngine::audioSampleTimer;
		greyoutChangeDirection = anythingNow ? 1 : -1;
		uiTimerManager.setTimer(TIMER_MATRIX_DRIVER, UI_MS_PER_REFRESH);
	}
}

void skipGreyoutFade() {

	if (greyoutChangeDirection > 0) {
		setGreyoutAmount(1);
	}
	else if (greyoutChangeDirection < 0) {
		setGreyoutAmount(0);
		greyoutCols = 0;
		greyoutRows = 0;
	}

	greyoutChangeDirection = 0;
}

void doGreyoutInstantly() {
	greyoutChangeDirection = 0;
	greyoutCols = 0xFFFFFFFF;
	greyoutRows = 0xFFFFFFFF;

	setGreyoutAmount(1);
}

void setGreyoutAmount(float newAmount) {
	greyProportion = newAmount * 6500000;
}

void timerRoutine() {
	// If output buffer is too full, come back in a little while instead
	if (uartGetTxBufferSpace(UART_ITEM_PIC_PADS) <= NUM_BYTES_IN_MAIN_PAD_REDRAW + NUM_BYTES_IN_SIDEBAR_REDRAW) {
		setTimerForSoon();
		return;
	}

	int progress;

	if (isUIModeActive(UI_MODE_HORIZONTAL_ZOOM)) {
		renderZoom();
	}

	else if (isUIModeActive(UI_MODE_HORIZONTAL_SCROLL)) {
		renderScroll();
	}

	else if (isUIModeActive(UI_MODE_AUDIO_CLIP_EXPANDING) || isUIModeActive(UI_MODE_AUDIO_CLIP_COLLAPSING)) {
		renderAudioClipExpandOrCollapse();
	}

	else if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING) || isUIModeActive(UI_MODE_INSTRUMENT_CLIP_EXPANDING)) {
		renderClipExpandOrCollapse();
	}

	else if (isUIModeActive(UI_MODE_NOTEROWS_EXPANDING_OR_COLLAPSING)) {
		renderNoteRowExpandOrCollapse();
	}

	else if (isUIModeActive(UI_MODE_EXPLODE_ANIMATION)) {
		progress = getTransitionProgress();
		if (progress >= 65536) { // If finished transitioning...

			// If going to keyboard screen, no sidebar or anything to fade in
			if (animationDirection == 1 && currentSong->currentClip->type == CLIP_TYPE_INSTRUMENT
			    && ((InstrumentClip*)currentSong->currentClip)->onKeyboardScreen) {
				currentUIMode = UI_MODE_NONE;
				changeRootUI(&keyboardScreen);
			}

			// Otherwise, there's stuff we want to fade in / to
			else {
				int explodedness = (animationDirection == 1) ? 65536 : 0;
				if (currentSong->currentClip->type == CLIP_TYPE_INSTRUMENT) {
					renderExplodeAnimation(explodedness, false);
				}
				else {
					renderAudioClipExplodeAnimation(explodedness, false);
				}
				memcpy(PadLEDs::imageStore, PadLEDs::image, (displayWidth + sideBarWidth) * displayHeight * 3);

				currentUIMode = UI_MODE_ANIMATION_FADE;
				if (animationDirection == 1) {
					if (currentSong->currentClip->type == CLIP_TYPE_INSTRUMENT) {
						changeRootUI(&instrumentClipView); // We want to fade the sidebar in
					}
					else {
						changeRootUI(&audioClipView);
						goto stopFade; // No need for fade since no sidebar, and also if we tried it'd get glitchy cos we're not set up for it
					}
				}
				else {
					changeRootUI(&arrangerView);

					if (arrangerView.doingAutoScrollNow)
						goto stopFade; // If we suddenly just started doing an auto-scroll, there's no time to fade
				}

				recordTransitionBegin(130);
				renderFade(0);
			}
		}
		else {
			int explodedness = (animationDirection == 1) ? 0 : 65536;
			explodedness += progress * animationDirection;

			if (currentSong->currentClip->type == CLIP_TYPE_INSTRUMENT) {
				renderExplodeAnimation(explodedness);
			}
			else {
				renderAudioClipExplodeAnimation(explodedness);
			}
		}
	}

	else if (isUIModeActive(UI_MODE_ANIMATION_FADE)) {
		progress = getTransitionProgress();
		if (progress >= 65536) {
stopFade:
			currentUIMode = UI_MODE_NONE;
			renderingNeededRegardlessOfUI(); // Just in case some waveforms couldn't be rendered when the store was written to, we want to re-render everything now
		}
		else {
			renderFade(progress);
		}
	}

	else {
		// Progress greyout
		if (greyoutChangeDirection != 0) {
			float amountDone = (float)(AudioEngine::audioSampleTimer - greyoutChangeStartTime) / greyoutSpeed;
			if (greyoutChangeDirection > 0) {
				if (amountDone > 1) {
					greyoutChangeDirection = 0;
					setGreyoutAmount(1);
				}
				else {
					setGreyoutAmount(amountDone);
					uiTimerManager.setTimer(TIMER_MATRIX_DRIVER, UI_MS_PER_REFRESH);
				}
			}
			else {
				// If we've finished exiting greyout mode
				if (amountDone > 1) {
					greyoutChangeDirection = 0;
					greyoutCols = 0;
					greyoutRows = 0;
				}
				else {
					setGreyoutAmount(1 - amountDone);
					uiTimerManager.setTimer(TIMER_MATRIX_DRIVER, UI_MS_PER_REFRESH);
				}
			}
			needToSendOutMainPadColours = needToSendOutSidebarColours = true;
		}
	}

	if (needToSendOutMainPadColours) sendOutMainPadColours();
	if (needToSendOutSidebarColours) sendOutSidebarColours();
}

void sendOutMainPadColours() {
	AudioEngine::logAction("sendOutMainPadColours 1");
	if (uartGetTxBufferSpace(UART_ITEM_PIC_PADS) <= NUM_BYTES_IN_MAIN_PAD_REDRAW) {
		sendOutMainPadColoursSoon();
		return;
	}

	for (int col = 0; col < displayWidth; col++) {
#if DELUGE_MODEL >= DELUGE_MODEL_144_PAD
		if (col & 1) sortLedsForCol(col - 1);
#else
		sortLedsForCol(col);
#endif
	}

	uartFlushIfNotSending(UART_ITEM_PIC_PADS);

	needToSendOutMainPadColours = false;

	AudioEngine::logAction("sendOutMainPadColours 2");
}

void sendOutMainPadColoursSoon() {
	needToSendOutMainPadColours = true;
	setTimerForSoon();
}

void sendOutSidebarColours() {

	if (uartGetTxBufferSpace(UART_ITEM_PIC_PADS) <= NUM_BYTES_IN_SIDEBAR_REDRAW) {
		sendOutSidebarColoursSoon();
		return;
	}

#if DELUGE_MODEL >= DELUGE_MODEL_144_PAD
	sortLedsForCol(displayWidth);
#else
	for (int col = 0; col < sideBarWidth; col++) {
		sortLedsForCol(col + displayWidth);
	}
#endif

	uartFlushIfNotSending(UART_ITEM_PIC_PADS);

	needToSendOutSidebarColours = false;
}

void sendOutSidebarColoursSoon() {
	needToSendOutSidebarColours = true;
	setTimerForSoon();
}

void setTimerForSoon() {
	if (!uiTimerManager.isTimerSet(TIMER_MATRIX_DRIVER)) {
		uiTimerManager.setTimer(TIMER_MATRIX_DRIVER, 20);
	}
}

void renderAudioClipExpandOrCollapse() {

	int progress = getTransitionProgress();
	if (isUIModeActive(UI_MODE_AUDIO_CLIP_EXPANDING)) {
		if (progress >= 65536) {
			currentUIMode = UI_MODE_NONE;
			changeRootUI(&audioClipView);
			return;
		}
	}

	else {
		// If collapse finished, switch to session view and do fade-in
		if (progress >= 65536) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

			memset(imageStore, 0, sizeof(imageStore));
			sessionView.renderRow(modelStack, waveformRenderer.collapseAnimationToWhichRow,
			                      imageStore[waveformRenderer.collapseAnimationToWhichRow],
			                      occupancyMaskStore[waveformRenderer.collapseAnimationToWhichRow], true);
			sessionView.finishedTransitioningHere();
			return;
		}
		progress = 65536 - progress;
	}

	renderAudioClipCollapseAnimation(progress);

	uiTimerManager.setTimer(TIMER_MATRIX_DRIVER, UI_MS_PER_REFRESH);
}

void renderClipExpandOrCollapse() {

	int progress = getTransitionProgress();
	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_EXPANDING)) {
		if (progress >= 65536) {
			currentUIMode = UI_MODE_NONE;

			if (((InstrumentClip*)currentSong->currentClip)->onKeyboardScreen) {
				changeRootUI(&keyboardScreen);
			}
			else {
				changeRootUI(&instrumentClipView);
				// If we need to zoom in horizontally because the Clip's too short...
				bool anyZoomingDone = instrumentClipView.zoomToMax(true);
				if (anyZoomingDone) uiNeedsRendering(&instrumentClipView, 0, 0xFFFFFFFF);
			}
			return;
		}
	}

	else {
		// If collapse finished, switch to session view and do fade-in
		if (progress >= 65536) {
			renderInstrumentClipCollapseAnimation(0, displayWidth + sideBarWidth, 0);
			memcpy(imageStore, PadLEDs::image, sizeof(PadLEDs::image));
			sessionView.finishedTransitioningHere();
			return;
		}
		progress = 65536 - progress;
	}

	renderInstrumentClipCollapseAnimation(0, displayWidth + sideBarWidth, progress);

	uiTimerManager.setTimer(TIMER_MATRIX_DRIVER, UI_MS_PER_REFRESH);
}

void renderNoteRowExpandOrCollapse() {
	int progress = getTransitionProgress();
	if (progress >= 65536) {
		currentUIMode = UI_MODE_NONE;
		uiNeedsRendering(&instrumentClipView);
		return;
	}

	renderInstrumentClipCollapseAnimation(0, displayWidth + 1, 65536 - progress);

	uiTimerManager.setTimer(TIMER_MATRIX_DRIVER, UI_MS_PER_REFRESH);
}

void renderZoom() {
	AudioEngine::logAction("MatrixDriver::renderZoom");

	int transitionProgress = getTransitionProgress();
	// If we've finished zooming...
	if (transitionProgress >= 65536) {
		exitUIMode(UI_MODE_HORIZONTAL_ZOOM);
		uiNeedsRendering(getCurrentUI(), 0xFFFFFFFF, 0);
		return;
	}

	if (!zoomingIn) transitionProgress = 65536 - transitionProgress;

	uint32_t sineValue = (getSine((transitionProgress + 98304) & 131071, 17) >> 16) + 32768;

	// The commented line is equivalent to the other lines just below
	//int32_t negativeFactorProgress = pow(zoomFactor, transitionProgress - 1) * 134217728; // Sorry, the purpose of this variable has got a bit cryptic, it exists after much simplification
	int32_t powersOfTwo = ((int32_t)(transitionProgress >> 7) - 512) << zoomMagnitude;
	int32_t fine = powersOfTwo & 1023;
	int32_t coarse = powersOfTwo >> 10;

	// Numbers below here represent 1 as 65536

	// inImageWidthComparedToNormal and outImageWidthComparedToNormal show how much bigger than "normal" those two images are to appear.
	// E.g. when fully zoomed out, the out-image would be "1" (65536), and the in-image would be "0.5" (32768). And so on.

	uint32_t inImageTimesBiggerThanNormal =
	    interpolateTable(fine, 10, expTableSmall); // This could be changed to run on a bigger number of bits in input
	inImageTimesBiggerThanNormal = increaseMagnitude(inImageTimesBiggerThanNormal, coarse - 14);

	renderZoomWithProgress(inImageTimesBiggerThanNormal, sineValue, &imageStore[0][0][0],
	                       &imageStore[displayHeight][0][0], 0, 0, displayWidth, displayWidth,
	                       displayWidth + sideBarWidth, displayWidth + sideBarWidth);

	sendOutMainPadColours();
	uiTimerManager.setTimer(TIMER_MATRIX_DRIVER, UI_MS_PER_REFRESH);
}

// inImageFadeAmount is how much of the in-image we'll see, out of 65536
void renderZoomWithProgress(int inImageTimesBiggerThanNative, uint32_t inImageFadeAmount,
                            uint8_t* __restrict__ innerImage, uint8_t* __restrict__ outerImage, int innerImageLeftEdge,
                            int outerImageLeftEdge, int innerImageRightEdge, int outerImageRightEdge,
                            int innerImageTotalWidth, int outerImageTotalWidth) {

	uint32_t outImageTimesBiggerThanNative = inImageTimesBiggerThanNative << zoomMagnitude;

	uint32_t inImageTimesSmallerThanNative =
	    4294967295u
	    / inImageTimesBiggerThanNative; // How many squares of the zoomed-in image fit into each square of our output image, at current zoom level
	uint32_t outImageTimesSmallerThanNative =
	    4294967295u
	    / outImageTimesBiggerThanNative; // How many squares of the zoomed-out image fit into each square of our output image, at current zoom level

	int32_t lastZoomPinSquareDone = 2147483647;

	// To save on stack usage, these arrays are stored in the string buffer
	int32_t* outputSquareStartOnInImage = (int32_t*)&miscStringBuffer[displayWidth * sizeof(int32_t) * 0];
	int32_t* outputSquareEndOnInImage = (int32_t*)&miscStringBuffer[displayWidth * sizeof(int32_t) * 1];
	int32_t* outputSquareStartOnOutImage = (int32_t*)&miscStringBuffer[displayWidth * sizeof(int32_t) * 2];
	int32_t* outputSquareEndOnOutImage = (int32_t*)&miscStringBuffer[displayWidth * sizeof(int32_t) * 3];
	uint16_t* inImageFadePerCol = (uint16_t*)shortStringBuffer; // 0 means show none. 65536 means show all, only
#define zoomPinSquareInner zoomPinSquare
#define zoomPinSquareOuter zoomPinSquare

	// Go through each row
	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		if (transitionTakingPlaceOnRow[yDisplay]) {

			// If this row doesn't have the same pin-square as the last, we have to calculate some stuff. Otherwise, this can be reused.
			if (zoomPinSquare[yDisplay] != lastZoomPinSquareDone) {
				lastZoomPinSquareDone = zoomPinSquare[yDisplay];

				// Work out what square the thinner image begins at (i.e. its left-most edge)
				int32_t inImagePos0Onscreen = zoomPinSquare[yDisplay]
				                              - (zoomPinSquareInner[yDisplay] >> 8)
				                                    * (inImageTimesBiggerThanNative >> 8); // Beware rounding inaccuracy
				int32_t inImageLeftEdgeOnscreen =
				    inImagePos0Onscreen + inImageTimesBiggerThanNative * innerImageLeftEdge;
				int32_t inImageRightEdgeOnscreen =
				    inImagePos0Onscreen + inImageTimesBiggerThanNative * innerImageRightEdge;

				// Do some pre-figuring-out for each column of the final-rendered image - which we can hopefully refer to for each row
				for (int xDisplay = 0; xDisplay < displayWidth; xDisplay++) {

					int32_t outputSquareLeftEdge = xDisplay * 65536;
					int32_t outputSquareRightEdge = outputSquareLeftEdge + 65536;

					// Work out how much of this square will be covered by the "in" (thinner) image (often it'll be all of it, or none)
					int32_t inImageOverlap = getMin(outputSquareRightEdge, inImageRightEdgeOnscreen)
					                         - getMax(outputSquareLeftEdge, inImageLeftEdgeOnscreen);
					if (inImageOverlap < 0) inImageOverlap = 0;

					// Convert that into knowing what proportion of colour from each image we want to grab
					inImageFadePerCol[xDisplay] = ((uint32_t)inImageOverlap * inImageFadeAmount) >> 16;

					int32_t outputSquareLeftEdgePositionRelativeToPinSquare =
					    zoomPinSquare[yDisplay] - outputSquareLeftEdge;

					int32_t outputSquareLeftEdgePositionOnInImageRelativeToPinSquare =
					    ((int64_t)outputSquareLeftEdgePositionRelativeToPinSquare * inImageTimesSmallerThanNative)
					    >> 16;
					int32_t outputSquareLeftEdgePositionOnOutImageRelativeToPinSquare =
					    ((int64_t)outputSquareLeftEdgePositionRelativeToPinSquare * outImageTimesSmallerThanNative)
					    >> 16;

					// Work out, for this square/col/pixel, the corresponding local coordinate for both the in- and out-images. Do that for both the leftmost and rightmost edge of this square
					outputSquareStartOnOutImage[xDisplay] =
					    zoomPinSquareOuter[yDisplay] - outputSquareLeftEdgePositionOnOutImageRelativeToPinSquare;
					outputSquareStartOnInImage[xDisplay] =
					    zoomPinSquareInner[yDisplay] - outputSquareLeftEdgePositionOnInImageRelativeToPinSquare;

					outputSquareEndOnInImage[xDisplay] =
					    outputSquareStartOnInImage[xDisplay] + inImageTimesSmallerThanNative;
					outputSquareEndOnOutImage[xDisplay] =
					    outputSquareStartOnOutImage[xDisplay] + outImageTimesSmallerThanNative;
				}
			}

			// Go through each column onscreen
			for (int xDisplay = 0; xDisplay < displayWidth; xDisplay++) {

				uint32_t outValue[3];
				memset(outValue, 0, sizeof(outValue));

				bool drawingAnything = false;

				if (inImageFadePerCol[xDisplay]) {

					renderZoomedSquare(outputSquareStartOnInImage[xDisplay], outputSquareEndOnInImage[xDisplay],
					                   inImageTimesBiggerThanNative, inImageFadePerCol[xDisplay], outValue, innerImage,
					                   innerImageRightEdge, &drawingAnything);
				}

				{
					renderZoomedSquare(outputSquareStartOnOutImage[xDisplay], outputSquareEndOnOutImage[xDisplay],
					                   outImageTimesBiggerThanNative, 65535 - inImageFadePerCol[xDisplay], outValue,
					                   outerImage, outerImageRightEdge, &drawingAnything);
				}

				if (drawingAnything) {
					for (int colour = 0; colour < 3; colour++) {
						int result = rshift_round(outValue[colour], 16);
						PadLEDs::image[yDisplay][xDisplay][colour] = getMin(255, result);
					}
				}
				else {
					memset(PadLEDs::image[yDisplay][xDisplay], 0, 3);
				}
			}
		}

		innerImage += innerImageTotalWidth * 3;
		outerImage += outerImageTotalWidth * 3;
	}
	if (DELUGE_MODEL != DELUGE_MODEL_40_PAD) {
		AudioEngine::routineWithClusterLoading(); // -----------------------------------
	}
}

void renderZoomedSquare(int32_t outputSquareStartOnSourceImage, int32_t outputSquareEndOnSourceImage,
                        uint32_t sourceImageTimesBiggerThanNormal, unsigned int sourceImageFade, uint32_t* output,
                        uint8_t* inputImageRow, int inputImageWidth, bool* drawingAnything) {

	int32_t outImageStartSquareLeftEdge = (uint32_t)(outputSquareStartOnSourceImage) & ~(uint32_t)65535;
	for (int32_t sourceSquareLeftEdge = getMax((int32_t)0, outImageStartSquareLeftEdge); true;
	     sourceSquareLeftEdge += 65536) {
		if (sourceSquareLeftEdge >= outputSquareEndOnSourceImage) break;
		int xSource = sourceSquareLeftEdge >> 16;
		if (xSource >= inputImageWidth) break;

		// If nothing (i.e. black) at this input pixel, continue
		if (!((*(uint32_t*)&inputImageRow[xSource * 3]) & (uint32_t)16777215)) continue;

		*drawingAnything = true;

		int32_t sourceSquareRightEdge = sourceSquareLeftEdge + 65536;
		uint32_t intensity = getMin(sourceSquareRightEdge, outputSquareEndOnSourceImage)
		                     - getMax(sourceSquareLeftEdge, outputSquareStartOnSourceImage); // Will end up at max 65536

		intensity = ((uint64_t)intensity * sourceImageFade * sourceImageTimesBiggerThanNormal) >> 32;

		for (int colour = 0; colour < 3; colour++) {
			output[colour] += (int)inputImageRow[xSource * 3 + colour] * intensity;
		}
	}
}

void renderScroll() {

	squaresScrolled++;
	int copyCol = (animationDirection > 0) ? squaresScrolled - 1 : areaToScroll - squaresScrolled;
	int startSquare = (animationDirection > 0) ? 0 : areaToScroll - 1;
	int endSquare = (animationDirection > 0) ? areaToScroll - 1 : 0;
	for (int row = 0; row < displayHeight; row++) {
		if (transitionTakingPlaceOnRow[row]) {
			for (int colour = 0; colour < 3; colour++) {
				for (int x = startSquare; x != endSquare; x += animationDirection) {
					PadLEDs::image[row][x][colour] = PadLEDs::image[row][x + animationDirection][colour];
				}
				// And, bring in a col from the temp image
				if (scrollingIntoNothing) PadLEDs::image[row][endSquare][colour] = 0;
				else PadLEDs::image[row][endSquare][colour] = imageStore[row][copyCol][colour];
			}

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
			bufferPICPadsUart(228 + row);
			sendRGBForOnePadFast(endSquare, row, image[row][endSquare]);
#endif
		}
	}

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	sendOutMainPadColours();
	if (areaToScroll > displayWidth) {
		sendOutSidebarColours();
	}
#else
	bufferPICPadsUart(240);
	uartFlushIfNotSending(UART_ITEM_PIC_PADS);
#endif
	if (squaresScrolled >= areaToScroll) {
		getCurrentUI()->scrollFinished();
	}
	else {
		uiTimerManager.setTimer(TIMER_MATRIX_DRIVER, UI_MS_PER_REFRESH_SCROLLING);
	}
}

void setupScroll(int8_t thisScrollDirection, uint8_t thisAreaToScroll, bool scrollIntoNothing, int numSquaresToScroll) {
	animationDirection = thisScrollDirection;
	areaToScroll = thisAreaToScroll;
	squaresScrolled = thisAreaToScroll - numSquaresToScroll;
	scrollingIntoNothing = scrollIntoNothing;

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	uint8_t flags = 0;
	if (thisScrollDirection >= 0) flags |= 1;
	if (thisAreaToScroll == displayWidth + sideBarWidth) flags |= 2;
	bufferPICPadsUart(236 + flags);
#endif

	renderScroll();
}

void renderFade(int progress) {
	for (int y = 0; y < displayHeight; y++) {
		for (int x = 0; x < displayWidth + sideBarWidth; x++) {
			for (int c = 0; c < 3; c++) {
				int difference = (int)imageStore[y + displayHeight][x][c] - (int)imageStore[y][x][c];
				int progressedDifference = rshift_round(difference * progress, 16);
				PadLEDs::image[y][x][c] = imageStore[y][x][c] + progressedDifference;
			}
		}
	}
	sendOutMainPadColours();
	sendOutSidebarColours();
	uiTimerManager.setTimer(TIMER_MATRIX_DRIVER, UI_MS_PER_REFRESH);
}

void recordTransitionBegin(unsigned int newTransitionLength) {
	clearPendingUIRendering();
	transitionLength = newTransitionLength * 44;
	transitionStartTime = AudioEngine::audioSampleTimer;
}

int getTransitionProgress() {
	return ((uint64_t)(AudioEngine::audioSampleTimer - transitionStartTime) * 65536) / transitionLength;
}

void copyBetweenImageStores(uint8_t* __restrict__ dest, uint8_t* __restrict__ source, int destWidth, int sourceWidth,
                            int copyWidth) {
	if (destWidth == sourceWidth && copyWidth >= sourceWidth - 2) {
		memcpy(dest, source, sourceWidth * displayHeight * 3);
	}
	else {
		uint8_t* destEndOverall = dest + destWidth * displayHeight * 3;
		do {
			memcpy(dest, source, copyWidth * 3);
			dest += destWidth * 3;
			source += sourceWidth * 3;
		} while (dest < destEndOverall);
	}
}

void moveBetweenImageStores(uint8_t* dest, uint8_t* source, int destWidth, int sourceWidth, int copyWidth) {

	uint8_t* destEndOverall = dest + destWidth * displayHeight * 3;
	do {
		memmove(dest, source, copyWidth * 3);
		dest += destWidth * 3;
		source += sourceWidth * 3;
	} while (dest < destEndOverall);
}

} // namespace PadLEDs
