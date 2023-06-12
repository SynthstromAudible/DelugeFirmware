/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include <ClipView.h>
#include "song.h"
#include "Clip.h"
#include "ActionLogger.h"
#include "matrixdriver.h"
#include "PlaybackMode.h"
#include "Session.h"
#include "numericdriver.h"
#include "playbackhandler.h"
#include "Buttons.h"
#include "extern.h"
#include "ClipMinder.h"
#include "View.h"
#include "definitions.h"

ClipView::ClipView() {
}

unsigned int ClipView::getMaxZoom() {
	return currentSong->currentClip->getMaxZoom();
}

uint32_t ClipView::getMaxLength() {
	return currentSong->currentClip->getMaxLength();
}

void ClipView::focusRegained() {
	ClipNavigationTimelineView::focusRegained();
}

int ClipView::buttonAction(int x, int y, bool on, bool inCardRoutine) {

	// Horizontal encoder button press-down - don't let it do its zoom level thing if zooming etc not currently accessible
	if (x == xEncButtonX && y == xEncButtonY && on && !currentSong->currentClip->currentlyScrollableAndZoomable()) {}

#ifdef BUTTON_SEQUENCE_DIRECTION_X
	else if (x == BUTTON_SEQUENCE_DIRECTION_X && y == BUTTON_SEQUENCE_DIRECTION_Y) {
		if (on && isNoUIModeActive()) {
			currentSong->currentClip->sequenceDirection++;
			if (currentSong->currentClip->sequenceDirection == NUM_SEQUENCE_DIRECTION_OPTIONS) {
				currentSong->currentClip->sequenceDirection = 0;
			}
			view.setModLedStates();
		}
	}
#endif
	else return ClipNavigationTimelineView::buttonAction(x, y, on, inCardRoutine);

	return ACTION_RESULT_DEALT_WITH;
}

extern bool allowResyncingDuringClipLengthChange;

// Check newLength valid before calling this
Action* ClipView::lengthenClip(int32_t newLength) {

	Action* action = NULL;

	// If the last action was a shorten, undo it
	bool undoing = (actionLogger.firstAction[BEFORE] && actionLogger.firstAction[BEFORE]->openForAdditions
	                && actionLogger.firstAction[BEFORE]->type == ACTION_CLIP_LENGTH_DECREASE
	                && actionLogger.firstAction[BEFORE]->currentClip == currentSong->currentClip);

	if (undoing) {
		allowResyncingDuringClipLengthChange =
		    false; // Little bit of a hack. We don't want any resyncing to happen to this Clip
		actionLogger.revert(BEFORE, false, false);
		allowResyncingDuringClipLengthChange = true;
	}

	// Only if that didn't get us directly to the correct length, manually set length. This will do a resync if playback active
	if (currentSong->currentClip->loopLength != newLength) {
		int actionType = (newLength < currentSong->currentClip->loopLength) ? ACTION_CLIP_LENGTH_DECREASE
		                                                                    : ACTION_CLIP_LENGTH_INCREASE;

		action = actionLogger.getNewAction(actionType, true);
		if (action && action->currentClip != currentSong->currentClip) {
			action = actionLogger.getNewAction(actionType, false);
		}

		currentSong->setClipLength(currentSong->currentClip, newLength, action);
	}

	// Otherwise, do the resync that we missed out on doing
	else {
		if (undoing && playbackHandler.isEitherClockActive()) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

			currentPlaybackMode->reSyncClip(modelStack);
		}
	}

	return action;
}

// Check newLength valid before calling this
Action* ClipView::shortenClip(int32_t newLength) {

	Action* action = NULL;

	action = actionLogger.getNewAction(ACTION_CLIP_LENGTH_DECREASE, true);
	if (action && action->currentClip != currentSong->currentClip) {
		action = actionLogger.getNewAction(ACTION_CLIP_LENGTH_DECREASE, false);
	}

	currentSong->setClipLength(
	    currentSong->currentClip, newLength,
	    action); // Subsequently shortening by more squares won't cause additional Consequences to be added to the same
	             // Action - it checks, and only stores the data (snapshots and original length) once
	return action;
}

int ClipView::horizontalEncoderAction(int offset) {

	// Shift button pressed - edit length
	if (isNoUIModeActive() && !Buttons::isButtonPressed(yEncButtonX, yEncButtonY)
	    && (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(clipViewButtonX, clipViewButtonY))) {

		// If tempoless recording, don't allow
		if (!currentSong->currentClip->currentlyScrollableAndZoomable()) {
			numericDriver.displayPopup(HAVE_OLED ? "Can't edit length" : "CANT");
			return ACTION_RESULT_DEALT_WITH;
		}

		uint32_t oldLength = currentSong->currentClip->loopLength;

		// If we're not scrolled all the way to the right, go there now
		if (scrollRightToEndOfLengthIfNecessary(oldLength)) return ACTION_RESULT_DEALT_WITH;

		// Or if still here, we've already scrolled far-right

		if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

		uint32_t newLength;

		Action* action = NULL;

		bool rightOnSquare;
		int32_t endSquare = getSquareFromPos(oldLength, &rightOnSquare);

		// Lengthening
		if (offset == 1) {

			newLength = getPosFromSquare(endSquare) + getLengthExtendAmount(endSquare);

			// If we're still within limits
			if (newLength <= (uint32_t)MAX_SEQUENCE_LENGTH) {

				action = lengthenClip(newLength);

				if (!scrollRightToEndOfLengthIfNecessary(newLength)) {
doReRender:
					uiNeedsRendering(this, 0xFFFFFFFF, 0);
				}
			}
		}

		// Shortening
		else {

			if (!rightOnSquare) {
				newLength = getPosFromSquare(endSquare);
			}
			else {
				newLength = oldLength - getLengthChopAmount(endSquare);
			}

			if (newLength > 0) {

				action = shortenClip(newLength);

				// Scroll / zoom as needed
				if (!scrollLeftIfTooFarRight(newLength)) {
					// If this zoom level no longer valid...
					if (zoomToMax(true)) {
						//editor.displayZoomLevel(true);
					}
					else {
						goto doReRender;
					}
				}
			}
		}

		displayNumberOfBarsAndBeats(newLength, currentSong->xZoom[NAVIGATION_CLIP], false, "LONG");

		if (action) action->xScrollClip[AFTER] = currentSong->xScroll[NAVIGATION_CLIP];
		return ACTION_RESULT_DEALT_WITH;
	}

	// Or, if shift button not pressed...
	else {

		// If tempoless recording, don't allow
		if (!currentSong->currentClip->currentlyScrollableAndZoomable()) {
			return ACTION_RESULT_DEALT_WITH;
		}

		// Otherwise, let parent do scrolling and zooming
		return ClipNavigationTimelineView::horizontalEncoderAction(offset);
	}
}

int32_t ClipView::getLengthChopAmount(int32_t square) {

	square--; // We want the width of the square before
	while (!isSquareDefined(square))
		square--;

	uint32_t xZoom = currentSong->xZoom[getNavSysId()];

	if (inTripletsView()) {
		if (xZoom < currentSong->tripletsLevel) {
			return xZoom * 4 / 3;
		}
		else if (xZoom < currentSong->tripletsLevel * 2) {
			return xZoom * 2 / 3 * (((square + 1) % 2) + 1);
		}
	}
	return xZoom;
}

int32_t ClipView::getLengthExtendAmount(int32_t square) {

	while (!isSquareDefined(square))
		square++;

	uint32_t xZoom = currentSong->xZoom[getNavSysId()];

	if (inTripletsView()) {
		if (xZoom < currentSong->tripletsLevel) {
			return xZoom * 4 / 3;
		}
		else if (xZoom < currentSong->tripletsLevel * 2) {
			return xZoom * 2 / 3 * (((square + 1) % 2) + 1);
		}
	}
	return xZoom;
}

int ClipView::getTickSquare() {

	int newTickSquare = getSquareFromPos(currentSong->currentClip->getLivePos());

	// See if we maybe want to do an auto-scroll
	if (currentSong->currentClip->getCurrentlyRecordingLinearly()) {

		if (newTickSquare == displayWidth && (!currentUIMode || currentUIMode == UI_MODE_AUDITIONING)
		    && getCurrentUI() == this && // currentPlaybackMode == &session &&
		    (!currentSong->currentClip->armState || xScrollBeforeFollowingAutoExtendingLinearRecording != -1)) {

			if (xScrollBeforeFollowingAutoExtendingLinearRecording == -1)
				xScrollBeforeFollowingAutoExtendingLinearRecording = currentSong->xScroll[NAVIGATION_CLIP];

			int32_t newXScroll =
			    currentSong->xScroll[NAVIGATION_CLIP] + currentSong->xZoom[NAVIGATION_CLIP] * displayWidth;

			horizontalScrollForLinearRecording(newXScroll);
		}
	}

	// Or if not, cancel following scrolling along, and go back to where we started
	else {
		if (xScrollBeforeFollowingAutoExtendingLinearRecording != -1) {
			int32_t newXScroll = xScrollBeforeFollowingAutoExtendingLinearRecording;
			xScrollBeforeFollowingAutoExtendingLinearRecording = -1;

			if (newXScroll != currentSong->xZoom[NAVIGATION_CLIP]) {
				horizontalScrollForLinearRecording(newXScroll);
			}
		}
	}

	return newTickSquare;
}
