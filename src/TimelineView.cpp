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
#include <InstrumentClipMinder.h>
#include <TimelineView.h>
#include "matrixdriver.h"
#include "string.h"
#include "song.h"
#include "songeditor.h"
#include "View.h"
#include "PadLEDs.h"
#include "numericdriver.h"
#include "IndicatorLEDs.h"
#include "Buttons.h"
#include "extern.h"
#include "oled.h"

extern "C" {
#include "cfunctions.h"
}

void TimelineView::scrollFinished() {
	exitUIMode(UI_MODE_HORIZONTAL_SCROLL);
	uiNeedsRendering(
	    this, 0xFFFFFFFF,
	    0); // Needed because sometimes we initiate a scroll before reverting an Action, so we need to properly render again afterwards
}

// Virtual function
bool TimelineView::setupScroll(uint32_t oldScroll) {
	memset(PadLEDs::transitionTakingPlaceOnRow, 1, sizeof(PadLEDs::transitionTakingPlaceOnRow));

	renderMainPads(0xFFFFFFFF, PadLEDs::imageStore, &PadLEDs::occupancyMaskStore[displayHeight], true);

	return true;
}

bool TimelineView::calculateZoomPinSquares(uint32_t oldScroll, uint32_t newScroll, uint32_t newZoom, uint32_t oldZoom) {

	int32_t zoomPinSquareBig = ((int64_t)(int32_t)(oldScroll - newScroll) << 16) / (int32_t)(newZoom - oldZoom);

	for (int i = 0; i < displayHeight; i++) {
		PadLEDs::zoomPinSquare[i] = zoomPinSquareBig;
	}

	tellMatrixDriverWhichRowsContainSomethingZoomable();

	return true;
}

int TimelineView::buttonAction(int x, int y, bool on, bool inCardRoutine) {

	// Horizontal encoder button
	if (x == xEncButtonX && y == xEncButtonY) {
		if (on) {
			// Show current zoom level
			if (isNoUIModeActive()) displayZoomLevel();

			enterUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
		}

		else {

			if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
				numericDriver.cancelPopup();
				exitUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
			}
		}
	}

	// Triplets button
	else if (x == tripletsButtonX && y == tripletsButtonY) {
		if (on) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			tripletsButtonPressed();
		}
	}

#ifdef soloButtonX
	// Solo button
	else if (x == soloButtonX && y == soloButtonY) {
		if (on) {
			if (isNoUIModeActive()) {
				enterUIMode(UI_MODE_SOLO_BUTTON_HELD);
				IndicatorLEDs::blinkLed(soloLedX, soloLedY, 255, 1);
			}
		}
		else {
			exitUIMode(UI_MODE_SOLO_BUTTON_HELD);
			IndicatorLEDs::setLedState(soloLedX, soloLedY, false);
		}
	}

#endif

	else return view.buttonAction(x, y, on, inCardRoutine);

	return ACTION_RESULT_DEALT_WITH;
}

void TimelineView::displayZoomLevel(bool justPopup) {

	char text[30];
	currentSong->getNoteLengthName(text, currentSong->xZoom[getNavSysId()], true);

	numericDriver.displayPopup(text, justPopup ? 3 : 0, true);
}

bool horizontalEncoderActionLock = false;
extern bool pendingUIRenderingLock;

int TimelineView::horizontalEncoderAction(int offset) {

	if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

	// These next two, I had here before adding the actual SD lock check / remind-later above. Maybe they're not still necessary? If either was true, wouldn't
	// sdRoutineLock be true also for us to have gotten here?
	if (pendingUIRenderingLock)
		return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // Would possibly prefer to have this case cause it to still come back later and do it, but oh well
	if (horizontalEncoderActionLock)
		return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // Really wouldn't want to get in here multiple times, while pre-rendering the waveforms for the new navigation
	horizontalEncoderActionLock = true;

	int navSysId = getNavSysId();

	// Encoder button pressed, zoom.
	if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {

		if (isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
			int oldXZoom = currentSong->xZoom[navSysId];

			int zoomMagnitude = -offset;

			// Constrain to zoom limits
			if (zoomMagnitude == -1) {
				if (currentSong->xZoom[navSysId] <= 3) goto getOut;
				currentSong->xZoom[navSysId] >>= 1;
			}
			else {
				if (currentSong->xZoom[navSysId] >= getMaxZoom()) goto getOut;
				currentSong->xZoom[navSysId] <<= 1;
			}

			uint32_t newZoom = currentSong->xZoom[navSysId];
			int32_t newScroll = currentSong->xScroll[navSysId] / (newZoom * displayWidth) * (newZoom * displayWidth);

			initiateXZoom(zoomMagnitude, newScroll, oldXZoom);
			displayZoomLevel();
		}
	}

	// Encoder button not pressed, scroll
	else if (isNoUIModeActive() || isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
		// If shift button not pressed
		if (!Buttons::isShiftButtonPressed()) {

			int32_t newXScroll = currentSong->xScroll[navSysId] + offset * currentSong->xZoom[navSysId] * displayWidth;

			// Make sure we don't scroll too far left
			if (newXScroll < 0) newXScroll = 0;

			// Make sure we don't scroll too far right
			if (newXScroll < getMaxLength() || offset < 0) {
				if (newXScroll != currentSong->xScroll[navSysId]) {
					initiateXScroll(newXScroll);
				}
			}
			displayScrollPos();
		}
	}

getOut:
	horizontalEncoderActionLock = false;
	return ACTION_RESULT_DEALT_WITH;
}

void TimelineView::displayScrollPos() {

	int navSysId = getNavSysId();
	uint32_t quantization = currentSong->xZoom[navSysId];
	if (navSysId == NAVIGATION_CLIP) quantization *= displayWidth;

	displayNumberOfBarsAndBeats(currentSong->xScroll[navSysId], quantization, true, "FAR");
}

void TimelineView::displayNumberOfBarsAndBeats(uint32_t number, uint32_t quantization, bool countFromOne,
                                               char const* tooLongText) {

	uint32_t oneBar = currentSong->getBarLength();

	unsigned int whichBar = number / oneBar;

	unsigned int posWithinBar = number - whichBar * oneBar;

	unsigned int whichBeat = posWithinBar / (oneBar >> 2);

	unsigned int posWithinBeat = posWithinBar - whichBeat * (oneBar >> 2);

	unsigned int whichSubBeat = posWithinBeat / (oneBar >> 4);

	if (countFromOne) {
		whichBar++;
		whichBeat++;
		whichSubBeat++;
	}

#if HAVE_OLED
	char text[15];
	intToString(whichBar, text);
	char* pos = strchr(text, 0);
	*(pos++) = ':';
	intToString(whichBeat, pos);
	pos = strchr(pos, 0);
	*(pos++) = ':';
	intToString(whichSubBeat, pos);
	OLED::popupText(text);
#else
	char text[5];

	uint8_t dotMask = 0b10000000;

	if (whichBar >= 10000) {
		strcpy(text, tooLongText);
	}
	else {
		strcpy(text, "    ");

		if (whichBar < 10) intToString(whichBar, &text[1]);
		else intToString(whichBar, &text[0]);

		if (whichBar < 100) {
			dotMask |= 1 << 2;

			if (quantization >= (oneBar >> 2)) {
				text[2] = ' ';
				goto putBeatCountOnFarRight;
			}

			intToString(whichBeat, &text[2]);
			dotMask |= 1 << 1;

			intToString(whichSubBeat, &text[3]);
		}
		else if (whichBar < 1000) {
			dotMask |= 1 << 1;

putBeatCountOnFarRight:
			intToString(whichBeat, &text[3]);
		}
	}

	numericDriver.displayPopup(text, 3, false, dotMask);
#endif
}

// Changes the actual xScroll.
void TimelineView::initiateXScroll(uint32_t newXScroll, int numSquaresToScroll) {

	uint32_t oldXScroll = currentSong->xScroll[getNavSysId()];

	int scrollDirection = (newXScroll > currentSong->xScroll[getNavSysId()]) ? 1 : -1;

	currentSong->xScroll[getNavSysId()] = newXScroll;

	bool anyAnimationToDo = setupScroll(oldXScroll);

	if (anyAnimationToDo) {
		currentUIMode |=
		    UI_MODE_HORIZONTAL_SCROLL; // Must set this before calling PadLEDs::setupScroll(), which might then unset it
		PadLEDs::setupScroll(scrollDirection, displayWidth, false, numSquaresToScroll);
	}
}

// Returns whether any zooming took place - I think?
bool TimelineView::zoomToMax(bool inOnly) {
	unsigned int maxZoom = getMaxZoom();
	uint32_t oldZoom = currentSong->xZoom[getNavSysId()];
	if (maxZoom != oldZoom && (!inOnly || maxZoom < oldZoom)) {

		// Zoom to view what's new
		currentSong->xZoom[getNavSysId()] = maxZoom;

		int32_t newScroll = currentSong->xScroll[getNavSysId()] / (maxZoom * displayWidth) * (maxZoom * displayWidth);

		initiateXZoom(howMuchMoreMagnitude(maxZoom, oldZoom), newScroll, oldZoom);
		return true;
	}
	else {
		return false;
	}
}

// Puts us into zoom mode. Assumes we've already altered currentSong->xZoom.
void TimelineView::initiateXZoom(int zoomMagnitude, int32_t newScroll, uint32_t oldZoom) {

	memcpy(PadLEDs::imageStore[(zoomMagnitude < 0) ? displayHeight : 0], PadLEDs::image,
	       (displayWidth + sideBarWidth) * displayHeight * 3);

	uint32_t oldScroll = currentSong->xScroll[getNavSysId()];

	currentSong->xScroll[getNavSysId()] = newScroll;
	bool anyToAnimate = calculateZoomPinSquares(oldScroll, newScroll, currentSong->xZoom[getNavSysId()], oldZoom);

	if (anyToAnimate) {

		int storeOffset = (zoomMagnitude < 0) ? 0 : displayHeight;

		renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[storeOffset], &PadLEDs::occupancyMaskStore[storeOffset], true);

		PadLEDs::zoomingIn = (zoomMagnitude < 0);
		PadLEDs::zoomMagnitude = PadLEDs::zoomingIn ? -zoomMagnitude : zoomMagnitude;

		enterUIMode(UI_MODE_HORIZONTAL_ZOOM);
		PadLEDs::recordTransitionBegin(zoomSpeed);
		PadLEDs::renderZoom();
	}
}

bool TimelineView::scrollRightToEndOfLengthIfNecessary(int32_t maxLength) {

	// If we're not scrolled all the way to the right, go there now
	if (getPosFromSquare(displayWidth) < maxLength) {

		uint32_t displayLength = currentSong->xZoom[getNavSysId()] * displayWidth;

		initiateXScroll((maxLength - 1) / displayLength * displayLength);
		//displayScrollPos();
		return true;
	}
	return false;
}

bool TimelineView::scrollLeftIfTooFarRight(int32_t maxLength) {

	if (getPosFromSquare(0) >= maxLength) {
		initiateXScroll(currentSong->xScroll[getNavSysId()] - currentSong->xZoom[getNavSysId()] * displayWidth);
		//displayScrollPos();
		return true;
	}
	return false;
}

void TimelineView::tripletsButtonPressed() {

	currentSong->tripletsOn = !currentSong->tripletsOn;
	if (currentSong->tripletsOn) currentSong->tripletsLevel = currentSong->xZoom[getNavSysId()] * 4 / 3;
	uiNeedsRendering(this, 0xFFFFFFFF, 0);
	view.setTripletsLedState();
}

int32_t TimelineView::getPosFromSquare(int32_t square, int32_t xScroll, uint32_t xZoom) {

	if (inTripletsView()) {
		// If zoomed in just a normal amount...
		if (xZoom < currentSong->tripletsLevel) {
			int32_t prevBlockStart = xScroll / (currentSong->tripletsLevel * 3) * (currentSong->tripletsLevel * 3);
			int32_t blockStartRelativeToScroll = (prevBlockStart - xScroll) / 3 * 4; // Negative or 0

			int32_t posRelativeToBlock =
			    ((int32_t)square) * (xZoom * 4 / 3) - blockStartRelativeToScroll; // Relative to block start pos
			uint32_t numBlocksIn = posRelativeToBlock / (currentSong->tripletsLevel * 4);

			// These two lines affect the resulting "pos" of cols which are "undefined" so they can be detected as such
			uint32_t numTripletsIn = posRelativeToBlock / currentSong->tripletsLevel;
			if (numTripletsIn % 4 == 3) posRelativeToBlock = (numBlocksIn + 1) * currentSong->tripletsLevel * 3;

			else posRelativeToBlock -= numBlocksIn * currentSong->tripletsLevel;

			return posRelativeToBlock + prevBlockStart;
		}
		else if (xZoom < currentSong->tripletsLevel * 2) {
			return xScroll + square * xZoom + (square % 2) * currentSong->tripletsLevel / 2;
		}
	}

	return xScroll + square * xZoom;
}

int32_t TimelineView::getPosFromSquare(int32_t square, int32_t xScroll) {

	int navSys = getNavSysId();

	if (xScroll == -1) {
		xScroll = currentSong->xScroll[navSys]; // Sets default
	}

	uint32_t xZoom = currentSong->xZoom[navSys];

	return getPosFromSquare(square, xScroll, xZoom);
}

int32_t TimelineView::getSquareFromPos(int32_t pos, bool* rightOnSquare, int32_t xScroll, uint32_t xZoom) {

	int32_t posRelativeToScroll = pos - xScroll;

	if (inTripletsView()) {
		if (xZoom < currentSong->tripletsLevel) {
			int32_t blockStartPos = xScroll / (currentSong->tripletsLevel * 3) * (currentSong->tripletsLevel * 3);
			int32_t blockStartRelativeToScroll = blockStartPos - xScroll; // Will be negative or 0
			int32_t posRelativeToBlockStart = pos - blockStartPos;

			if (rightOnSquare)
				*rightOnSquare =
				    (posRelativeToBlockStart % (xZoom * 4 / 3) == 0); // Will the % be ok if it's negative? No! :O

			int32_t numBlocksIn =
			    divide_round_negative(posRelativeToBlockStart,
			                          currentSong->tripletsLevel * 3); // Keep as separate step, for rounding purposes

			int32_t semiFinal =
			    posRelativeToBlockStart + blockStartRelativeToScroll * 4 / 3 + numBlocksIn * currentSong->tripletsLevel;
			int32_t final = divide_round_negative(semiFinal, xZoom * 4 / 3);
			return final;
		}
		else if (xZoom < currentSong->tripletsLevel * 2) {
			int posRelativeToTripletsStart =
			    posRelativeToScroll % (currentSong->tripletsLevel * 3); // Will the % be ok if it's negative? No! :O
			if (rightOnSquare)
				*rightOnSquare =
				    (posRelativeToTripletsStart == 0 || posRelativeToTripletsStart == currentSong->tripletsLevel * 2);

			if (posRelativeToTripletsStart >= currentSong->tripletsLevel * 2)
				posRelativeToTripletsStart -= currentSong->tripletsLevel * 2;
			return divide_round_negative(posRelativeToScroll - posRelativeToTripletsStart, xZoom);
		}
	}

	if (rightOnSquare)
		*rightOnSquare = (posRelativeToScroll >= 0
		                  && (posRelativeToScroll % xZoom) == 0); // Will the % be ok if it's negative? No! :O

	// Have to divide the two things separately before subtracting, otherwise negative results get rounded the wrong way!
	return divide_round_negative(pos - xScroll, xZoom);
}

int32_t TimelineView::getSquareFromPos(int32_t pos, bool* rightOnSquare, int32_t xScroll) {

	int navSys = getNavSysId();

	if (xScroll == -1) {
		xScroll = currentSong->xScroll[navSys]; // Defaults to main currentSong->xScroll
	}

	uint32_t xZoom = currentSong->xZoom[navSys];

	return getSquareFromPos(pos, rightOnSquare, xScroll, xZoom);
}

int32_t TimelineView::getSquareEndFromPos(int32_t pos, int32_t localScroll) {
	bool rightOnSquare;
	int32_t square = getSquareFromPos(pos, &rightOnSquare, localScroll);
	if (!rightOnSquare) square++;
	return square;
}

bool TimelineView::isSquareDefined(int square, int32_t xScroll, uint32_t xZoom) {
	if (!inTripletsView()) return true;
	if (xZoom > currentSong->tripletsLevel) return true;
	return (getPosFromSquare(square + 1, xScroll, xZoom) > getPosFromSquare(square, xScroll, xZoom));
}

// Deprecate this.
bool TimelineView::isSquareDefined(int square, int32_t xScroll) {
	if (!inTripletsView()) return true;
	else {
		if (currentSong->xZoom[getNavSysId()] > currentSong->tripletsLevel) return true;
		return (getPosFromSquare(square + 1, xScroll) > getPosFromSquare(square, xScroll));
	}
}

bool TimelineView::inTripletsView() {
	return (supportsTriplets() && currentSong->tripletsOn);
}

void TimelineView::midiLearnFlash() {
	uiNeedsRendering(this, 0, 0xFFFFFFFF);
}
