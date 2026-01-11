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

#include "hid/led/pad_leds.h"
#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "gui/menu_item/colour.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "gui/waveform/waveform_render_data.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"

#include <cstring>
#include <limits>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

using namespace deluge;

namespace PadLEDs {
RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth];                      // 255 = full brightness
uint8_t occupancyMask[kDisplayHeight][kDisplayWidth + kSideBarWidth];          // 64 = full occupancy
RGB imageStore[kDisplayHeight * 2][kDisplayWidth + kSideBarWidth];             // 255 = full brightness
uint8_t occupancyMaskStore[kDisplayHeight * 2][kDisplayWidth + kSideBarWidth]; // 64 = full occupancy

bool zoomingIn;
int8_t zoomMagnitude;
int32_t zoomPinSquare[kDisplayHeight];
bool transitionTakingPlaceOnRow[kDisplayHeight];
int8_t explodeAnimationDirection;
UI* explodeAnimationTargetUI = nullptr;

namespace horizontal {
uint8_t areaToScroll;
uint8_t squaresScrolled;
int8_t scrollDirection;
bool scrollingIntoNothing; // Means we're scrolling into a black screen
} // namespace horizontal

namespace vertical {
uint8_t squaresScrolled;
int8_t scrollDirection;
bool scrollingToNothing;
} // namespace vertical

int16_t animatedRowGoingTo[kMaxNumAnimatedRows];
int16_t animatedRowGoingFrom[kMaxNumAnimatedRows];
uint8_t numAnimatedRows;

int32_t greyProportion;
int8_t greyoutChangeDirection;
unsigned long greyoutChangeStartTime;

bool needToSendOutMainPadColours;
bool needToSendOutSidebarColours;

uint8_t flashCursor;

uint8_t slowFlashSquares[kDisplayHeight];
uint8_t slowFlashColours[kDisplayHeight];

int32_t explodeAnimationYOriginBig;
int32_t explodeAnimationXStartBig;
int32_t explodeAnimationXWidthBig;

// We stash these here for during UI-transition animation, because if that's happening as part of an undo, the Sample
// might not be there anymore
int32_t sampleValueCentrePoint;
int32_t sampleValueSpan;
int32_t sampleMaxPeakFromZero;
WaveformRenderData waveformRenderData;
RGB audioClipColour;
bool sampleReversed;

// Same for InstrumentClips
int32_t clipLength;
RGB clipMuteSquareColour;

bool renderingLock;

uint32_t transitionLength;
uint32_t transitionStartTime;

uint32_t greyoutCols;
uint32_t greyoutRows;

void init() {
	memset(slowFlashSquares, 255, sizeof(slowFlashSquares));
}

bool shouldNotRenderDuringTimerRoutine() {
	return (renderingLock || currentUIMode == UI_MODE_EXPLODE_ANIMATION || currentUIMode == UI_MODE_IMPLODE_ANIMATION
	        || currentUIMode == UI_MODE_ANIMATION_FADE || currentUIMode == UI_MODE_HORIZONTAL_ZOOM
	        || currentUIMode == UI_MODE_HORIZONTAL_SCROLL || currentUIMode == UI_MODE_INSTRUMENT_CLIP_EXPANDING
	        || currentUIMode == UI_MODE_INSTRUMENT_CLIP_COLLAPSING
	        || currentUIMode == UI_MODE_NOTEROWS_EXPANDING_OR_COLLAPSING);
}

void clearTickSquares(bool shouldSend) {

	uint32_t colsToSend = 0;

	if (flashCursor == FLASH_CURSOR_SLOW && !shouldNotRenderDuringTimerRoutine()) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {

			if (slowFlashSquares[y] != 255) {
				colsToSend |= (1 << (slowFlashSquares[y] >> 1));
			}
		}
	}

	memset(slowFlashSquares, 255, sizeof(slowFlashSquares));

	if (shouldSend && flashCursor == FLASH_CURSOR_SLOW && !shouldNotRenderDuringTimerRoutine()) {
		if (colsToSend) {
			for (int32_t x = 0; x < 8; x++) {
				if (colsToSend & (1 << x)) {
					if (uartGetTxBufferSpace(UART_ITEM_PIC_PADS) <= kNumBytesInColUpdateMessage) {
						break;
					}

					sortLedsForCol(x << 1);
				}
			}
			PIC::flush();
		}
	}
}

void setTickSquares(const uint8_t* squares, const uint8_t* colours) {

	uint32_t colsToSend = 0;

	if (flashCursor == FLASH_CURSOR_SLOW) {
		if (!shouldNotRenderDuringTimerRoutine()) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				if (squares[y] != slowFlashSquares[y] || colours[y] != slowFlashColours[y]) {

					// Remember to update the new column
					if (squares[y] != 255) {
						colsToSend |= (1 << (squares[y] >> 1));
					}

					// And the old column
					if (slowFlashSquares[y] != 255) {
						colsToSend |= (1 << (slowFlashSquares[y] >> 1));
					}
				}
			}
		}
	}
	else if (flashCursor == FLASH_CURSOR_FAST) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (squares[y] != slowFlashSquares[y] && squares[y] != 255) {

				int32_t colour = 0;
				if (colours[y] == 1) { // "Muted" colour
					RGB mutedColour = gui::menu_item::mutedColourMenu.getRGB();
					auto transform = [](uint8_t& channel, size_t idx) {
						if (channel >= 64) {
							channel += (1 << idx);
						}
					};

					transform(mutedColour.r, 0);
					transform(mutedColour.g, 1);
					transform(mutedColour.b, 2);
				}
				else if (colours[y] == 2) { // Red
					colour = 0b00000001;
				}

				PadLEDs::flashMainPad(squares[y], y, colour);
			}
		}
	}

	memcpy(slowFlashSquares, squares, kDisplayHeight);
	memcpy(slowFlashColours, colours, kDisplayHeight);

	if (flashCursor == FLASH_CURSOR_SLOW && !shouldNotRenderDuringTimerRoutine()) {
		// Actually send everything, if there was a change
		if (colsToSend) {
			for (int32_t x = 0; x < 8; x++) {
				if (colsToSend & (1 << x)) {
					if (uartGetTxBufferSpace(UART_ITEM_PIC_PADS) <= kNumBytesInColUpdateMessage) {
						break;
					}
					sortLedsForCol(x << 1);
				}
			}
			PIC::flush();
		}
	}
}

void clearAllPadsWithoutSending() {
	memset(image, 0, sizeof(image));
}

void clearMainPadsWithoutSending() {
	for (auto& y : image) {
		std::fill(&y[0], &y[kDisplayWidth], gui::colours::black);
	}
}

void clearSideBar() {
	for (auto& y : image) {
		y[kDisplayWidth] = gui::colours::black;
		y[kDisplayWidth + 1] = gui::colours::black;
	}

	sendOutSidebarColours();
}

void clearColumnWithoutSending(int32_t x) {
	for (auto& y : image) {
		y[x] = gui::colours::black;
	}
}

RGB prepareColour(int32_t x, int32_t y, RGB colourSource);

// You'll want to call uartFlushToPICIfNotSending() after this
void sortLedsForCol(int32_t x) {
	AudioEngine::logAction("MatrixDriver::sortLedsForCol");

	x &= 0b11111110;

	std::array<RGB, kDisplayHeight * 2> doubleColumn{};
	size_t total = 0;
	for (size_t y = 0; y < kDisplayHeight; y++) {
		doubleColumn[total++] = prepareColour(x, y, image[y][x]);
	}
	for (size_t y = 0; y < kDisplayHeight; y++) {
		doubleColumn[total++] = prepareColour(x + 1, y, image[y][x + 1]);
	}
	PIC::setColourForTwoColumns((x >> 1), doubleColumn);
}

const RGB flashColours[3] = {
    {130, 120, 130},
    gui::colours::muted, // Not used anymore
    gui::colours::red,
};

RGB prepareColour(int32_t x, int32_t y, RGB colourSource) {
	if (flashCursor == FLASH_CURSOR_SLOW && slowFlashSquares[y] == x && currentUIMode != UI_MODE_HORIZONTAL_SCROLL) {
		if (slowFlashColours[y] == 1) { // If it's to be the "muted" colour, get that
			colourSource = gui::menu_item::mutedColourMenu.getRGB();
		}
		else { // Otherwise, pull from a referenced table line
			colourSource = flashColours[slowFlashColours[y]];
		}
	}

	if ((greyoutRows || greyoutCols)
	    && ((greyoutRows & (1 << y)) || (greyoutCols & (1 << (kDisplayWidth + kSideBarWidth - 1 - x))))) {
		return colourSource.greyOut(greyProportion);
	}
	return colourSource;
}

void set(Cartesian pad, RGB colour) {
	image[pad.y][pad.x] = colour;
}

void writeToSideBar(uint8_t sideBarX, uint8_t yDisplay, uint8_t red, uint8_t green, uint8_t blue) {
	image[yDisplay][sideBarX + kDisplayWidth] = RGB(red, green, blue);
}

void setupInstrumentClipCollapseAnimation(bool collapsingOutOfClipMinder) {
	clipLength = getCurrentClip()->loopLength;

	if (collapsingOutOfClipMinder) {
		// This shouldn't have to be done every time
		clipMuteSquareColour = view.getClipMuteSquareColour(getCurrentClip(), clipMuteSquareColour);
	}
}

void renderInstrumentClipCollapseAnimation(int32_t xStart, int32_t xEndOverall, int32_t progress) {
	AudioEngine::logAction("MatrixDriver::renderCollapseAnimation");

	memset(image, 0, sizeof(image));
	memset(occupancyMask, 0, sizeof(occupancyMask));

	if (!(isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING) || isUIModeActive(UI_MODE_INSTRUMENT_CLIP_EXPANDING))) {
		for (int32_t row = 0; row < kDisplayHeight; row++) {
			image[row][kDisplayWidth] = gui::colours::enabled;
			occupancyMask[row][kDisplayWidth] = 64;
		}
	}

	// Do some pre figuring out, which applies to all columns
	uint16_t* intensity1Array = (uint16_t*)miscStringBuffer;
	uint16_t* intensity2Array = (uint16_t*)&miscStringBuffer[kMaxNumAnimatedRows * sizeof(uint16_t)];
	int8_t newRowPosition1Array[kMaxNumAnimatedRows];

	for (int32_t i = 0; i < numAnimatedRows; i++) {
		int32_t newRowPosition = (int32_t)animatedRowGoingFrom[i] * 65536
		                         + ((int32_t)animatedRowGoingTo[i] - animatedRowGoingFrom[i]) * (65536 - progress);
		newRowPosition1Array[i] = newRowPosition >> 16;
		intensity2Array[i] = newRowPosition; // & 65535;
		intensity1Array[i] = 65535 - intensity2Array[i];
	}

	int32_t greyStart =
	    instrumentClipView.getSquareFromPos(clipLength - 1, NULL, currentSong->xScroll[NAVIGATION_CLIP]) + 1;
	int32_t xEnd = std::min(kDisplayWidth, greyStart);

	int32_t greyTop, greyBottom;

	if (currentUIMode == UI_MODE_NOTEROWS_EXPANDING_OR_COLLAPSING) {
		greyTop = kDisplayHeight;
		greyBottom = 0;
	}

	else {
		greyTop = animatedRowGoingTo[0] + 1 + (((kDisplayHeight - animatedRowGoingTo[0]) * progress + 32768) >> 16);
		greyBottom = animatedRowGoingTo[0] - (((animatedRowGoingTo[0]) * progress + 32768) >> 16);
		if (greyTop > kDisplayHeight) {
			greyTop = kDisplayHeight;
		}
		if (greyBottom < 0) {
			greyBottom = 0;
		}
	}

	if (xEnd < kDisplayWidth) {

		if (xEnd < 0) {
			xEnd = 0;
		}

		for (int32_t yDisplay = greyBottom; yDisplay < greyTop; yDisplay++) {
			auto* begin = &image[yDisplay][xEnd];
			std::fill(begin, begin + (kDisplayWidth - xEnd), deluge::gui::colours::grey);
		}
	}

	for (int32_t col = xStart; col < xEndOverall; col++) {

		if (col < kDisplayWidth) {
			if (col >= xEnd) {
				continue; // It's beyond the end of the Clip, and it's already been filled in grey
			}

			// Or if it's greyed out cos of triplets...
			if (!instrumentClipView.isSquareDefined(col, currentSong->xScroll[NAVIGATION_CLIP])) {
				for (int32_t yDisplay = greyBottom; yDisplay < greyTop; yDisplay++) {
					PadLEDs::image[yDisplay][col] = gui::colours::grey;
				}
				continue;
			}
		}

		for (int32_t i = 0; i < numAnimatedRows; i++) {
			if (!occupancyMaskStore[i][col]) {
				continue; // Nothing to do if there was nothing in this square
			}

			RGB& squareColours = imageStore[i][col];

			int32_t intensity1 = intensity1Array[i];
			int32_t intensity2 = intensity2Array[i];

			if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)
			    || isUIModeActive(UI_MODE_INSTRUMENT_CLIP_EXPANDING)) {

				// If the audition column, fade it out as we go
				if (col == kDisplayWidth + kSideBarWidth - 1) {
					intensity1 = ((uint32_t)intensity1 * progress) >> 16;
					intensity2 = ((uint32_t)intensity2 * progress) >> 16;
				}

				// If the mute-col, we want to alter the colour
				if (col == kDisplayWidth) {
					squareColours = RGB::blend(squareColours, clipMuteSquareColour, progress);
				}
			}

			if (newRowPosition1Array[i] >= 0 && newRowPosition1Array[i] < kDisplayHeight) {
				PadLEDs::image[newRowPosition1Array[i]][col] =
				    drawSquare(squareColours, intensity1, PadLEDs::image[newRowPosition1Array[i]][col],
				               &occupancyMask[newRowPosition1Array[i]][col], occupancyMaskStore[i][col]);
			}

			if (newRowPosition1Array[i] >= -1 && newRowPosition1Array[i] < kDisplayHeight - 1) {
				PadLEDs::image[newRowPosition1Array[i] + 1][col] =
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
	audioClipColour = clip->getColour();

	sampleReversed = clip->sampleControls.isCurrentlyReversed();

	Sample* sample = (Sample*)clip->sampleHolder.audioFile;

	if (ALPHA_OR_BETA_VERSION && !sample) {
		FREEZE_WITH_ERROR("E311");
	}

	sampleMaxPeakFromZero = sample->getMaxPeakFromZero();
	sampleValueCentrePoint = sample->getFoundValueCentrePoint();
	sampleValueSpan = sample->getValueSpan();

	waveformRenderData = clip->renderData;
}

void renderAudioClipCollapseAnimation(int32_t progress) {
	memset(image, 0, sizeof(image));

	int32_t endSquareDisplay = divide_round_negative(
	    clipLength - currentSong->xScroll[NAVIGATION_CLIP] - 1,
	    currentSong->xZoom[NAVIGATION_CLIP]); // Rounds it well down, so we get the "final square" kinda...
	int32_t greyStart = endSquareDisplay + 1;
	int32_t xEnd = std::min(kDisplayWidth, greyStart);

	for (int32_t col = 0; col < xEnd; col++) {
		waveformRenderer.renderOneColForCollapseAnimation(col, col, sampleMaxPeakFromZero, progress, PadLEDs::image,
		                                                  &waveformRenderData, audioClipColour, sampleReversed,
		                                                  sampleValueCentrePoint, sampleValueSpan);
	}

	if (xEnd < kDisplayWidth) {

		if (xEnd < 0) {
			xEnd = 0;
		}

		int32_t greyTop =
		    waveformRenderer.collapseAnimationToWhichRow + 1
		    + (((kDisplayHeight - waveformRenderer.collapseAnimationToWhichRow) * progress + 32768) >> 16);
		int32_t greyBottom = waveformRenderer.collapseAnimationToWhichRow
		                     - (((waveformRenderer.collapseAnimationToWhichRow) * progress + 32768) >> 16);

		if (greyTop > kDisplayHeight) {
			greyTop = kDisplayHeight;
		}
		if (greyBottom < 0) {
			greyBottom = 0;
		}

		for (int32_t yDisplay = greyBottom; yDisplay < greyTop; yDisplay++) {
			auto* begin = &PadLEDs::image[yDisplay][xEnd];
			std::fill(begin, begin + (kDisplayWidth - xEnd), gui::colours::grey);
		}
	}

	// What about the sidebar, did I just not animate that?

	sendOutMainPadColours();
}

// 2^16 used in place of "1" in "big" arithmetic below
void renderAudioClipExplodeAnimation(int32_t explodedness, bool shouldSendOut) {

	memset(PadLEDs::image, 0, sizeof(PadLEDs::image));
	memset(occupancyMask, 0, sizeof(occupancyMask));

	int32_t startBigNow = (((int64_t)explodeAnimationXStartBig * (65536 - explodedness)) >> 16);
	int32_t widthBigWhenExploded = (kDisplayWidth << 16);
	int32_t widthBigWhenNotExploded = explodeAnimationXWidthBig;
	int32_t difference = widthBigWhenExploded - widthBigWhenNotExploded;
	int32_t widthBigNow = widthBigWhenNotExploded + (((int64_t)difference * explodedness) >> 16);

	int32_t inverseScale = ((uint64_t)widthBigWhenExploded << 16) / widthBigNow;

	int32_t xSourceRightEdge;

	for (int32_t xDestSquareRightEdge = 0; xDestSquareRightEdge <= kDisplayWidth; xDestSquareRightEdge++) {

		int32_t xSourceLeftEdge =
		    xSourceRightEdge; // What was the last square's right edge is now the current square's left edge

		// From here on, we talk about the right edge of the destination square
		int32_t xDestBig = xDestSquareRightEdge << 16;
		int32_t xDestBigRelativeToStart = xDestBig - startBigNow;

		int32_t xSourceBigRelativeToStartNow = ((int64_t)xDestBigRelativeToStart * inverseScale) >> 16;
		int32_t xSourceBig = xSourceBigRelativeToStartNow;
		xSourceRightEdge = xSourceBig >> 16;

		// For first iteration, we just wanted that value, to use next time - and we should get out now
		if (!xDestSquareRightEdge) {
			continue;
		}

		if (xSourceRightEdge <= 0) {
			continue; // <=0 probably looks a little bit better than <0
		}

		// Ok, we need the max values between xSourceLeftEdge and xSourceRightEdge
		int32_t xSourceLeftEdgeLimited = std::max(xSourceLeftEdge, 0_i32);
		int32_t xSourceRightEdgeLimited = std::min(xSourceRightEdge, kDisplayWidth);

		int32_t xDest = xDestSquareRightEdge - 1;
		waveformRenderer.renderOneColForCollapseAnimationZoomedOut(
		    xSourceLeftEdgeLimited, xSourceRightEdgeLimited, xDest, sampleMaxPeakFromZero, explodedness, PadLEDs::image,
		    &waveformRenderData, audioClipColour, sampleReversed, sampleValueCentrePoint, sampleValueSpan);

		if (xSourceRightEdge >= kDisplayWidth) {
			break; // If we got to the right edge of everything we want to draw onscreen
		}
	}

	if (shouldSendOut) {
		sendOutMainPadColours();
		uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, 35);
	}
}

// 2^16 used in place of "1" in "big" arithmetic below
void renderExplodeAnimation(int32_t explodedness, bool shouldSendOut) {
	memset(image, 0, sizeof(image));
	memset(occupancyMask, 0, sizeof(occupancyMask));

	// Set up some stuff for each x-pos that we don't want to be constantly re-calculating
	int32_t xDestArray[kDisplayWidth];
	uint16_t xIntensityArray[kDisplayWidth][2];

	int32_t xStart = 0;
	int32_t xEnd = kDisplayWidth;

	for (int32_t xSource = 0; xSource < kDisplayWidth; xSource++) {
		int32_t xSourceBig = xSource << 16;
		int32_t xOriginBig = explodeAnimationXStartBig
		                     + (((int64_t)explodeAnimationXWidthBig * xSourceBig) >> (kDisplayWidthMagnitude + 16));
		// xOriginBig = std::min(xOriginBig, explodeAnimationXStartBig + explodeAnimationXWidthBig - 65536);

		xOriginBig &= ~(uint32_t)65535; // Make sure each pixel's "origin-point" is right on an exact square - rounded
		                                // to the left. That'll match what we'll see in the arranger

		int32_t xSourceBigRelativeToOrigin = xSourceBig - xOriginBig;
		int32_t xDestBig = xOriginBig + (((int64_t)xSourceBigRelativeToOrigin * explodedness) >> 16);

		// Ok, so we're gonna squish this source square amongst 4 destination squares
		xDestArray[xSource] = xDestBig >> 16;

		// May as well narrow things down if we know now that some xSources won't end up onscreen
		if (xDestArray[xSource] < -1) {
			xStart = xSource + 1;
			continue;
		}
		else if (xDestArray[xSource] >= kDisplayWidth) {
			xEnd = xSource;
			break;
		}
		xIntensityArray[xSource][1] = xDestBig; // & 65535;
		xIntensityArray[xSource][0] = 65535 - xIntensityArray[xSource][1];
	}

	for (int32_t ySource = -1; ySource < kDisplayHeight + 1; ySource++) {

		int32_t ySourceBig = ySource << 16;
		int32_t ySourceBigRelativeToOrigin = ySourceBig - explodeAnimationYOriginBig;
		int32_t yDestBig = explodeAnimationYOriginBig + (((int64_t)ySourceBigRelativeToOrigin * explodedness) >> 16);
		int32_t yDest = yDestBig >> 16;

		uint32_t yIntensity[2];
		yIntensity[1] = yDestBig & 65535;
		yIntensity[0] = 65535 - yIntensity[1];

		for (int32_t xSource = xStart; xSource < xEnd; xSource++) {

			if (occupancyMaskStore[ySource + 1][xSource]) { // If there's actually anything in this source square...

				for (int32_t xOffset = 0; xOffset < 2; xOffset++) {
					int32_t xNow = xDestArray[xSource] + xOffset;
					if (xNow < 0) {
						continue;
					}
					if (xNow >= kDisplayWidth) {
						break;
					}

					for (int32_t yOffset = 0; yOffset < 2; yOffset++) {
						int32_t yNow = yDest + yOffset;
						if (yNow < 0) {
							continue;
						}
						if (yNow >= kDisplayHeight) {
							break;
						}

						uint32_t intensityNow = (yIntensity[yOffset] * xIntensityArray[xSource][xOffset]) >> 16;
						PadLEDs::image[yNow][xNow] =
						    drawSquare(imageStore[ySource + 1][xSource], intensityNow, PadLEDs::image[yNow][xNow],
						               &occupancyMask[yNow][xNow], occupancyMaskStore[ySource + 1][xSource]);
					}
				}
			}
		}
	}

	if (shouldSendOut) {
		sendOutMainPadColours();
		// Nice small number of milliseconds here. This animation is prone to looking jerky
		uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, 35);
	}
}

void reassessGreyout(bool doInstantly) {
	auto [newCols, newRows] = getUIGreyoutColsAndRows();

	// If same as before, get out
	if (newCols == greyoutCols && newRows == greyoutRows) {
		return;
	}

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
		uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
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

int32_t refreshTime = 23;
int32_t dimmerInterval = 0;

void setBrightnessLevel(uint8_t offset) {
	return setDimmerInterval(kMaxLedBrightness - offset);
}

void setRefreshTime(int32_t newTime) {
	PIC::setRefreshTime(newTime);
	refreshTime = newTime;
}

void changeRefreshTime(int32_t offset) {
	int32_t newTime = refreshTime + offset;
	if (newTime > 255 || newTime < 1) {
		return;
	}
	setRefreshTime(newTime);
	char buffer[12];
	intToString(refreshTime, buffer);
	display->displayPopup(buffer);
}

void changeDimmerInterval(int32_t offset) {
	int32_t newInterval = dimmerInterval - offset;
	if (newInterval > 25 || newInterval < 0) {}
	else {
		setDimmerInterval(newInterval);
	}

	if (display->haveOLED()) {
		char text[20];
		strcpy(text, "Brightness: ");
		char* pos = strchr(text, 0);
		intToString((25 - dimmerInterval) << 2, pos);
		pos = strchr(text, 0);
		*(pos++) = '%';
		*pos = 0;
		display->popupTextTemporary(text);
	}
}

void setDimmerInterval(int32_t newInterval) {
	// Uart::print("dimmerInterval: ");
	// Uart::println(newInterval);
	dimmerInterval = newInterval;

	int32_t newRefreshTime = 23 - newInterval;
	while (newRefreshTime < 8) {
		newRefreshTime++;
		newInterval *= 1.2; // (From Roha, Nov 2023) Hmm, not sure why this was necessary...
	}

	// Uart::print("newInterval: ");
	// Uart::println(newInterval);

	setRefreshTime(newRefreshTime);
	PIC::setDimmerInterval(newInterval);
}

void timerRoutine() {
	// If output buffer is too full, come back in a little while instead
	if (uartGetTxBufferSpace(UART_ITEM_PIC_PADS) <= kNumBytesInMainPadRedraw + kNumBytesInSidebarRedraw) {
		setTimerForSoon();
		return;
	}

	int32_t progress;

	if (isUIModeActive(UI_MODE_HORIZONTAL_ZOOM)) {
		renderZoom();
	}

	else if (isUIModeActive(UI_MODE_HORIZONTAL_SCROLL)) {
		horizontal::renderScroll();
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

	else if (isUIModeActive(UI_MODE_EXPLODE_ANIMATION) || isUIModeActive(UI_MODE_IMPLODE_ANIMATION)) {
		Clip* clip = getCurrentClip();

		progress = getTransitionProgress();
		if (progress >= 65536) { // If finished transitioning...

			// If going to keyboard screen, no sidebar or anything to fade in
			if (explodeAnimationDirection == 1 && clip->type == ClipType::INSTRUMENT
			    && ((InstrumentClip*)clip)->onKeyboardScreen) {
				currentUIMode = UI_MODE_NONE;
				changeRootUI(&keyboardScreen);
			}

			// Otherwise, there's stuff we want to fade in / to
			else {
				int32_t explodedness = (explodeAnimationDirection == 1) ? 65536 : 0;
				if ((clip->type == ClipType::INSTRUMENT) || (clip->onAutomationClipView)) {
					renderExplodeAnimation(explodedness, false);
				}
				else {
					renderAudioClipExplodeAnimation(explodedness, false);
				}
				memcpy(PadLEDs::imageStore, PadLEDs::image,
				       (kDisplayWidth + kSideBarWidth) * kDisplayHeight * sizeof(RGB));

				bool anyZoomingDone = false;
				currentUIMode = UI_MODE_ANIMATION_FADE;
				if (explodeAnimationDirection == 1) {
					if (clip->onAutomationClipView) {
						changeRootUI(&automationView); // We want to fade the sidebar in
						anyZoomingDone = instrumentClipView.zoomToMax(true);
						if (anyZoomingDone) {
							uiNeedsRendering(&automationView, 0, 0xFFFFFFFF);
						}
					}
					else if (clip->type == ClipType::INSTRUMENT) {
						changeRootUI(&instrumentClipView); // We want to fade the sidebar in
						anyZoomingDone = instrumentClipView.zoomToMax(true);
						if (anyZoomingDone) {
							uiNeedsRendering(&instrumentClipView, 0, 0xFFFFFFFF);
						}
					}
					else {
						changeRootUI(&audioClipView);
						goto stopFade; // No need for fade since no sidebar, and also if we tried it'd get glitchy cos
						               // we're not set up for it
					}
				}
				else {
					UI* nextUI = &arrangerView;
					if (explodeAnimationTargetUI != nullptr) {
						nextUI = explodeAnimationTargetUI;
						explodeAnimationTargetUI = nullptr;
					}

					changeRootUI(nextUI);

					if (nextUI == &arrangerView && arrangerView.doingAutoScrollNow) {
						goto stopFade; // If we suddenly just started doing an auto-scroll, there's no time to fade
					}
					else if (nextUI == &sessionView) {
						sessionView.finishedTransitioningHere();
					}
				}

				// if you zoomed in and re-rendered the sidebar, pause the animation
				// we'll continue the transition and render the fade after the next refresh
				// this ensures that the sidebar doesn't get rendered empty
				if (anyZoomingDone) {
					uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
				}
				// continue transition and render the fade
				else {
					recordTransitionBegin(130);
					renderFade(0);
				}
			}
		}
		else {
			int32_t explodedness = (explodeAnimationDirection == 1) ? 0 : 65536;
			explodedness += progress * explodeAnimationDirection;

			if ((clip->type == ClipType::INSTRUMENT) || (clip->onAutomationClipView)) {
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
			renderingNeededRegardlessOfUI(); // Just in case some waveforms couldn't be rendered when the store was
			                                 // written to, we want to re-render everything now
		}
		else {
			renderFade(progress);
		}
	}

	else {
		// Progress greyout
		if (greyoutChangeDirection != 0) {
			float amountDone = (float)(AudioEngine::audioSampleTimer - greyoutChangeStartTime) / kGreyoutSpeed;
			if (greyoutChangeDirection > 0) {
				if (amountDone > 1) {
					greyoutChangeDirection = 0;
					setGreyoutAmount(1);
				}
				else {
					setGreyoutAmount(amountDone);
					uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
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
					uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
				}
			}
			needToSendOutMainPadColours = needToSendOutSidebarColours = true;
		}
	}

	if (needToSendOutMainPadColours) {
		sendOutMainPadColours();
	}
	if (needToSendOutSidebarColours) {
		sendOutSidebarColours();
	}
}

void sendOutMainPadColours() {
	AudioEngine::logAction("sendOutMainPadColours 1");
	if (uartGetTxBufferSpace(UART_ITEM_PIC_PADS) <= kNumBytesInMainPadRedraw) {
		sendOutMainPadColoursSoon();
		return;
	}

	for (int32_t col = 0; col < kDisplayWidth; col++) {
		if (col & 1) {
			sortLedsForCol(col - 1);
		}
	}

	PIC::flush();

	needToSendOutMainPadColours = false;

	AudioEngine::logAction("sendOutMainPadColours 2");
}

void sendOutMainPadColoursSoon() {
	needToSendOutMainPadColours = true;
	setTimerForSoon();
}

void sendOutSidebarColours() {

	if (uartGetTxBufferSpace(UART_ITEM_PIC_PADS) <= kNumBytesInSidebarRedraw) {
		sendOutSidebarColoursSoon();
		return;
	}

	sortLedsForCol(kDisplayWidth);

	PIC::flush();

	needToSendOutSidebarColours = false;
}

void sendOutSidebarColoursSoon() {
	needToSendOutSidebarColours = true;
	setTimerForSoon();
}

void setTimerForSoon() {
	if (!uiTimerManager.isTimerSet(TimerName::MATRIX_DRIVER)) {
		uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, 20);
	}
}

void renderAudioClipExpandOrCollapse() {

	int32_t progress = getTransitionProgress();
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

	uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
}

void renderClipExpandOrCollapse() {
	int32_t progress = getTransitionProgress();
	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_EXPANDING)) {
		if (progress >= 65536) {
			currentUIMode = UI_MODE_NONE;

			Clip* clip = getCurrentClip();

			bool onKeyboardScreen = ((clip->type == ClipType::INSTRUMENT) && ((InstrumentClip*)clip)->onKeyboardScreen);

			// when transitioning back to clip, if keyboard view is enabled, it takes precedent
			// over automation and instrument clip views.
			if (clip->onAutomationClipView && !onKeyboardScreen) {
				changeRootUI(&automationView);
				// If we need to zoom in horizontally because the Clip's too short...
				bool anyZoomingDone = instrumentClipView.zoomToMax(true);
				if (anyZoomingDone) {
					uiNeedsRendering(&automationView, 0, 0xFFFFFFFF);
				}
			}
			else {
				if (onKeyboardScreen) {
					changeRootUI(&keyboardScreen);
				}
				else {
					changeRootUI(&instrumentClipView);
					// If we need to zoom in horizontally because the Clip's too short...
					bool anyZoomingDone = instrumentClipView.zoomToMax(true);
					if (anyZoomingDone) {
						uiNeedsRendering(&instrumentClipView, 0, 0xFFFFFFFF);
					}
				}
			}
			return;
		}
	}

	else {
		// If collapse finished, switch to session view and do fade-in
		if (progress >= 65536) {
			renderInstrumentClipCollapseAnimation(0, kDisplayWidth + kSideBarWidth, 0);
			memcpy(imageStore, PadLEDs::image, sizeof(PadLEDs::image));
			sessionView.finishedTransitioningHere();
			return;
		}
		progress = 65536 - progress;
	}

	renderInstrumentClipCollapseAnimation(0, kDisplayWidth + kSideBarWidth, progress);

	uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
}

void renderNoteRowExpandOrCollapse() {
	int32_t progress = getTransitionProgress();
	if (progress >= 65536) {
		currentUIMode = UI_MODE_NONE;
		if (getCurrentClip()->onAutomationClipView) {
			uiNeedsRendering(&automationView);
		}
		else {
			uiNeedsRendering(&instrumentClipView);
		}
		return;
	}

	renderInstrumentClipCollapseAnimation(0, kDisplayWidth + 1, 65536 - progress);

	uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
}

void renderZoom() {
	AudioEngine::logAction("MatrixDriver::renderZoom");

	int32_t transitionProgress = getTransitionProgress();
	// If we've finished zooming...
	if (transitionProgress >= 65536) {
		exitUIMode(UI_MODE_HORIZONTAL_ZOOM);
		uiNeedsRendering(getCurrentUI(), 0xFFFFFFFF, 0);
		return;
	}

	if (!zoomingIn) {
		transitionProgress = 65536 - transitionProgress;
	}

	uint32_t sineValue = (getSine((transitionProgress + 98304) & 131071, 17) >> 16) + 32768;

	// The commented line is equivalent to the other lines just below
	// int32_t negativeFactorProgress = pow(zoomFactor, transitionProgress - 1) * 134217728; // Sorry, the purpose of
	// this variable has got a bit cryptic, it exists after much simplification
	int32_t powersOfTwo = ((int32_t)(transitionProgress >> 7) - 512) << zoomMagnitude;
	int32_t fine = powersOfTwo & 1023;
	int32_t coarse = powersOfTwo >> 10;

	// Numbers below here represent 1 as 65536

	// inImageWidthComparedToNormal and outImageWidthComparedToNormal show how much bigger than "normal" those two
	// images are to appear. E.g. when fully zoomed out, the out-image would be "1" (65536), and the in-image would be
	// "0.5" (32768). And so on.

	uint32_t inImageTimesBiggerThanNormal =
	    interpolateTable(fine, 10, expTableSmall); // This could be changed to run on a bigger number of bits in input
	inImageTimesBiggerThanNormal = increaseMagnitude(inImageTimesBiggerThanNormal, coarse - 14);

	renderZoomWithProgress(inImageTimesBiggerThanNormal, sineValue, &imageStore[0][0][0],
	                       &imageStore[kDisplayHeight][0][0], 0, 0, kDisplayWidth, kDisplayWidth,
	                       kDisplayWidth + kSideBarWidth, kDisplayWidth + kSideBarWidth);

	sendOutMainPadColours();
	uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
}

// inImageFadeAmount is how much of the in-image we'll see, out of 65536
void renderZoomWithProgress(int32_t inImageTimesBiggerThanNative, uint32_t inImageFadeAmount,
                            uint8_t* __restrict__ innerImage, uint8_t* __restrict__ outerImage,
                            int32_t innerImageLeftEdge, int32_t outerImageLeftEdge, int32_t innerImageRightEdge,
                            int32_t outerImageRightEdge, int32_t innerImageTotalWidth, int32_t outerImageTotalWidth) {

	uint32_t outImageTimesBiggerThanNative = inImageTimesBiggerThanNative << zoomMagnitude;

	uint32_t inImageTimesSmallerThanNative =
	    4294967295u / inImageTimesBiggerThanNative; // How many squares of the zoomed-in image fit into each square of
	                                                // our output image, at current zoom level
	uint32_t outImageTimesSmallerThanNative =
	    4294967295u / outImageTimesBiggerThanNative; // How many squares of the zoomed-out image fit into each square of
	                                                 // our output image, at current zoom level

	int32_t lastZoomPinSquareDone = 2147483647;

	// To save on stack usage, these arrays are stored in the string buffer
	int32_t* outputSquareStartOnInImage = (int32_t*)&miscStringBuffer[kDisplayWidth * sizeof(int32_t) * 0];
	int32_t* outputSquareEndOnInImage = (int32_t*)&miscStringBuffer[kDisplayWidth * sizeof(int32_t) * 1];
	int32_t* outputSquareStartOnOutImage = (int32_t*)&miscStringBuffer[kDisplayWidth * sizeof(int32_t) * 2];
	int32_t* outputSquareEndOnOutImage = (int32_t*)&miscStringBuffer[kDisplayWidth * sizeof(int32_t) * 3];
	uint16_t* inImageFadePerCol = (uint16_t*)shortStringBuffer; // 0 means show none. 65536 means show all, only
#define zoomPinSquareInner zoomPinSquare
#define zoomPinSquareOuter zoomPinSquare

	// Go through each row
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (transitionTakingPlaceOnRow[yDisplay]) {

			// If this row doesn't have the same pin-square as the last, we have to calculate some stuff. Otherwise,
			// this can be reused.
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

				// Do some pre-figuring-out for each column of the final-rendered image - which we can hopefully refer
				// to for each row
				for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

					int32_t outputSquareLeftEdge = xDisplay * 65536;
					int32_t outputSquareRightEdge = outputSquareLeftEdge + 65536;

					// Work out how much of this square will be covered by the "in" (thinner) image (often it'll be all
					// of it, or none)
					int32_t inImageOverlap = std::min(outputSquareRightEdge, inImageRightEdgeOnscreen)
					                         - std::max(outputSquareLeftEdge, inImageLeftEdgeOnscreen);
					if (inImageOverlap < 0) {
						inImageOverlap = 0;
					}

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

					// Work out, for this square/col/pixel, the corresponding local coordinate for both the in- and
					// out-images. Do that for both the leftmost and rightmost edge of this square
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
			for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {

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
					for (int32_t colour = 0; colour < 3; colour++) {
						int32_t result = rshift_round(outValue[colour], 16);
						PadLEDs::image[yDisplay][xDisplay][colour] =
						    std::min<int32_t>(std::numeric_limits<uint8_t>::max(), result);
					}
				}
				else {
					PadLEDs::image[yDisplay][xDisplay] = gui::colours::black;
				}
			}
		}

		innerImage += innerImageTotalWidth * 3;
		outerImage += outerImageTotalWidth * 3;
	}
	AudioEngine::routineWithClusterLoading();
}

void renderZoomedSquare(int32_t outputSquareStartOnSourceImage, int32_t outputSquareEndOnSourceImage,
                        uint32_t sourceImageTimesBiggerThanNormal, uint32_t sourceImageFade, uint32_t* output,
                        uint8_t* inputImageRow, int32_t inputImageWidth, bool* drawingAnything) {

	int32_t outImageStartSquareLeftEdge = (uint32_t)(outputSquareStartOnSourceImage) & ~(uint32_t)65535;
	for (int32_t sourceSquareLeftEdge = std::max((int32_t)0, outImageStartSquareLeftEdge); true;
	     sourceSquareLeftEdge += 65536) {
		if (sourceSquareLeftEdge >= outputSquareEndOnSourceImage) {
			break;
		}
		int32_t xSource = sourceSquareLeftEdge >> 16;
		if (xSource >= inputImageWidth) {
			break;
		}

		// If nothing (i.e. black) at this input pixel, continue
		if (!((*(uint32_t*)&inputImageRow[xSource * 3]) & (uint32_t)16777215)) {
			continue;
		}

		*drawingAnything = true;

		int32_t sourceSquareRightEdge = sourceSquareLeftEdge + 65536;
		uint32_t intensity =
		    std::min(sourceSquareRightEdge, outputSquareEndOnSourceImage)
		    - std::max(sourceSquareLeftEdge, outputSquareStartOnSourceImage); // Will end up at max 65536

		intensity = ((uint64_t)intensity * sourceImageFade * sourceImageTimesBiggerThanNormal) >> 32;

		for (int32_t colour = 0; colour < 3; colour++) {
			output[colour] += (int32_t)inputImageRow[xSource * 3 + colour] * intensity;
		}
	}
}

void horizontal::renderScroll() {

	squaresScrolled++;
	int32_t copyCol = (scrollDirection > 0) ? squaresScrolled - 1 : areaToScroll - squaresScrolled;
	int32_t startSquare = (scrollDirection > 0) ? 0 : areaToScroll - 1;
	int32_t endSquare = (scrollDirection > 0) ? areaToScroll - 1 : 0;
	for (int32_t row = 0; row < kDisplayHeight; row++) {
		if (transitionTakingPlaceOnRow[row]) {
			for (int32_t colour = 0; colour < 3; colour++) {
				for (int32_t x = startSquare; x != endSquare; x += scrollDirection) {
					PadLEDs::image[row][x][colour] = PadLEDs::image[row][x + scrollDirection][colour];
				}
				// And, bring in a col from the temp image
				if (scrollingIntoNothing) {
					PadLEDs::image[row][endSquare][colour] = 0;
				}
				else {
					PadLEDs::image[row][endSquare][colour] = imageStore[row][copyCol][colour];
				}
			}

			PIC::sendScrollRow(row, prepareColour(endSquare, row, image[row][endSquare]));
		}
	}

	PIC::doneSendingRows();
	PIC::flush();

	if (squaresScrolled >= areaToScroll) {
		getCurrentUI()->scrollFinished();
	}
	else {
		uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH_SCROLLING);
	}
}

void horizontal::setupScroll(int8_t thisScrollDirection, uint8_t thisAreaToScroll, bool scrollIntoNothing,
                             int32_t numSquaresToScroll) {
	scrollDirection = thisScrollDirection;
	areaToScroll = thisAreaToScroll;
	squaresScrolled = thisAreaToScroll - numSquaresToScroll;
	scrollingIntoNothing = scrollIntoNothing;

	uint8_t flags = 0;
	if (thisScrollDirection >= 0) {
		flags |= 1;
	}
	if (thisAreaToScroll == kDisplayWidth + kSideBarWidth) {
		flags |= 2;
	}
	PIC::setupHorizontalScroll(flags);
	renderScroll();
}

void vertical::renderScroll() {
	squaresScrolled++;
	int32_t copyRow = (scrollDirection > 0) ? squaresScrolled - 1 : kDisplayHeight - squaresScrolled;
	int32_t startSquare = (scrollDirection > 0) ? 0 : 1;
	int32_t endSquare = (scrollDirection > 0) ? kDisplayHeight - 1 : 0;

	// matrixDriver.greyoutMinYDisplay = (scrollDirection > 0) ? kDisplayHeight - squaresScrolled : squaresScrolled;

	// Move the scrolling region
	memmove(image[startSquare], image[1 - startSquare],
	        (kDisplayWidth + kSideBarWidth) * (kDisplayHeight - 1) * sizeof(RGB));

	// And, bring in a row from the temp image (or from nowhere)
	if (scrollingToNothing) {
		memset(image[endSquare], 0, (kDisplayWidth + kSideBarWidth) * 3);
	}
	else {
		memcpy(image[endSquare], imageStore[copyRow], (kDisplayWidth + kSideBarWidth) * sizeof(RGB));
	}

	std::array<RGB, kDisplayWidth + kSideBarWidth> colours{};
	for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
		colours[x] = prepareColour(x, endSquare, image[endSquare][x]);
	}
	PIC::doVerticalScroll(scrollDirection > 0, colours);
	PIC::flush();
}

void vertical::setupScroll(int8_t thisScrollDirection, bool scrollIntoNothing) {
	scrollDirection = thisScrollDirection;
	scrollingToNothing = scrollIntoNothing;
	squaresScrolled = 0;
}

void renderFade(int32_t progress) {
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kDisplayWidth + kSideBarWidth; x++) {
			PadLEDs::image[y][x] = RGB::transform2(
			    imageStore[y][x], imageStore[y + kDisplayHeight][x], [progress](auto channelA, auto channelB) {
				    int32_t difference = (int32_t)channelB - (int32_t)channelA;
				    uint32_t progressedDifference = rshift_round(difference * progress, 16);
				    return channelA + progressedDifference;
			    });
		}
	}
	sendOutMainPadColours();
	sendOutSidebarColours();
	uiTimerManager.setTimer(TimerName::MATRIX_DRIVER, UI_MS_PER_REFRESH);
}

void recordTransitionBegin(uint32_t newTransitionLength) {
	clearPendingUIRendering();
	transitionLength = newTransitionLength * 44;
	transitionStartTime = AudioEngine::audioSampleTimer;
}

int32_t getTransitionProgress() {
	return ((uint64_t)(AudioEngine::audioSampleTimer - transitionStartTime) * 65536) / transitionLength;
}

void copyBetweenImageStores(RGB* __restrict__ dest, RGB* __restrict__ source, int32_t destWidth, int32_t sourceWidth,
                            int32_t copyWidth) {
	if (destWidth == sourceWidth && copyWidth >= sourceWidth - 2) {
		memcpy(dest, source, sourceWidth * kDisplayHeight * sizeof(RGB));
		return;
	}

	RGB* destEndOverall = dest + destWidth * kDisplayHeight;
	for (; dest < destEndOverall; dest += destWidth, source += sourceWidth) {
		memcpy(dest, source, copyWidth * sizeof(RGB));
	}
}

void moveBetweenImageStores(uint8_t* dest, uint8_t* source, int32_t destWidth, int32_t sourceWidth, int32_t copyWidth) {

	uint8_t* destEndOverall = dest + destWidth * kDisplayHeight * 3;
	do {
		memmove(dest, source, copyWidth * 3);
		dest += destWidth * 3;
		source += sourceWidth * 3;
	} while (dest < destEndOverall);
}

} // namespace PadLEDs
