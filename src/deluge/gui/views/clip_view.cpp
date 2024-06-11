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

#include "gui/views/clip_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/consequence/consequence_clip_horizontal_shift.h"
#include "model/song/song.h"
#include "playback/mode/playback_mode.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"

ClipView::ClipView() {
}

uint32_t ClipView::getMaxZoom() {
	return getCurrentClip()->getMaxZoom();
}

uint32_t ClipView::getMaxLength() {
	return getCurrentClip()->getMaxLength();
}

void ClipView::focusRegained() {
	ClipNavigationTimelineView::focusRegained();
}

ActionResult ClipView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Horizontal encoder button press-down - don't let it do its zoom level thing if zooming etc not currently
	// accessible
	if (b == X_ENC && on && !getCurrentClip()->currentlyScrollableAndZoomable()) {}

#ifdef BUTTON_SEQUENCE_DIRECTION_X
	else if (x == BUTTON_SEQUENCE_DIRECTION_X && y == BUTTON_SEQUENCE_DIRECTION_Y) {
		if (on && isNoUIModeActive()) {
			getCurrentClip()->sequenceDirection++;
			if (getCurrentClip()->sequenceDirection == NUM_SEQUENCE_DIRECTION_OPTIONS) {
				getCurrentClip()->sequenceDirection = 0;
			}
			view.setModLedStates();
		}
	}
#endif
	else {
		return ClipNavigationTimelineView::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}

extern bool allowResyncingDuringClipLengthChange;

// Check newLength valid before calling this
Action* ClipView::lengthenClip(int32_t newLength) {

	Action* action = NULL;

	// If the last action was a shorten, undo it
	bool undoing = (actionLogger.firstAction[BEFORE] && actionLogger.firstAction[BEFORE]->openForAdditions
	                && actionLogger.firstAction[BEFORE]->type == ActionType::CLIP_LENGTH_DECREASE
	                && actionLogger.firstAction[BEFORE]->currentClip == getCurrentClip());

	if (undoing) {
		allowResyncingDuringClipLengthChange =
		    false; // Little bit of a hack. We don't want any resyncing to happen to this Clip
		actionLogger.revert(BEFORE, false, false);
		allowResyncingDuringClipLengthChange = true;
	}

	// Only if that didn't get us directly to the correct length, manually set length. This will do a resync if playback
	// active
	if (getCurrentClip()->loopLength != newLength) {
		ActionType actionType = (newLength < getCurrentClip()->loopLength) ? ActionType::CLIP_LENGTH_DECREASE
		                                                                   : ActionType::CLIP_LENGTH_INCREASE;

		action = actionLogger.getNewAction(actionType, ActionAddition::ALLOWED);
		if (action && action->currentClip != getCurrentClip()) {
			action = actionLogger.getNewAction(actionType, ActionAddition::NOT_ALLOWED);
		}

		currentSong->setClipLength(getCurrentClip(), newLength, action);
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

	action = actionLogger.getNewAction(ActionType::CLIP_LENGTH_DECREASE, ActionAddition::ALLOWED);
	if (action && action->currentClip != getCurrentClip()) {
		action = actionLogger.getNewAction(ActionType::CLIP_LENGTH_DECREASE, ActionAddition::NOT_ALLOWED);
	}

	currentSong->setClipLength(
	    getCurrentClip(), newLength,
	    action); // Subsequently shortening by more squares won't cause additional Consequences to be added to the same
	// Action - it checks, and only stores the data (snapshots and original length) once
	return action;
}

ActionResult ClipView::horizontalEncoderAction(int32_t offset) {

	// Shift button pressed - edit length
	if (isNoUIModeActive() && !Buttons::isButtonPressed(deluge::hid::button::Y_ENC)
	    && Buttons::isShiftButtonPressed()) {

		// If tempoless recording, don't allow
		if (!getCurrentClip()->currentlyScrollableAndZoomable()) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_EDIT_LENGTH));
			return ActionResult::DEALT_WITH;
		}

		uint32_t oldLength = getCurrentClip()->loopLength;

		// If we're not scrolled all the way to the right, go there now
		if (scrollRightToEndOfLengthIfNecessary(oldLength)) {
			return ActionResult::DEALT_WITH;
		}

		// Or if still here, we've already scrolled far-right

		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		Action* action = NULL;

		uint32_t newLength = changeClipLength(offset, oldLength, action);

		displayNumberOfBarsAndBeats(newLength, currentSong->xZoom[NAVIGATION_CLIP], false, "LONG");

		if (action) {
			action->xScrollClip[AFTER] = currentSong->xScroll[NAVIGATION_CLIP];
		}
		return ActionResult::DEALT_WITH;
	}

	// Or, maybe shift everything horizontally
	else if ((isNoUIModeActive() && Buttons::isButtonPressed(deluge::hid::button::Y_ENC))
	         || (isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)
	             && Buttons::isButtonPressed(deluge::hid::button::CLIP_VIEW))) {
		// Just be safe - maybe not necessary
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		int32_t squareSize = getPosFromSquare(1) - getPosFromSquare(0);
		int32_t shiftAmount = offset * squareSize;
		Clip* clip = getCurrentClip();

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

		UI* currentUI = getCurrentUI();

		// Always shift automation when in Automation View
		// or also shift automation when default setting to only shift automation in Automation View is false
		bool shiftAutomation = (currentUI == &automationView || !FlashStorage::automationShift);

		// Always shift Notes and MPE when you're not in Automation View
		bool shiftSequenceAndMPE = (currentUI != &automationView);

		bool wasShifted = clip->shiftHorizontally(modelStack, shiftAmount, shiftAutomation, shiftSequenceAndMPE);
		if (!wasShifted) {
			// No need to show the user why it didnt succeed, usually these cases are fairly trivial
			return ActionResult::DEALT_WITH;
		}

		uiNeedsRendering(this, 0xFFFFFFFF, 0);

		// If possible, just modify a previous Action to add this new shift amount to it.
		Action* action = actionLogger.firstAction[BEFORE];
		if (action && action->type == ActionType::CLIP_HORIZONTAL_SHIFT && action->openForAdditions
		    && action->currentClip == clip) {

			// If there's no Consequence in the Action, that's probably because we deleted it a previous time with the
			// code just below. Or possibly because the Action was created but there wasn't enough RAM to create the
			// Consequence. Anyway, just go add a consequence now.
			if (!action->firstConsequence)
				goto addConsequenceToAction;

			ConsequenceClipHorizontalShift* consequence = (ConsequenceClipHorizontalShift*)action->firstConsequence;
			consequence->amount += shiftAmount;

			// It might look tempting that if we've completed one whole loop, we could delete the Consequence because
			// everything would be back the same - but no! Remember different NoteRows might have different lengths.
		}

		// Or if no previous Action, go create a new one now.
		else {

			action = actionLogger.getNewAction(ActionType::CLIP_HORIZONTAL_SHIFT, ActionAddition::NOT_ALLOWED);
			if (action) {
addConsequenceToAction:
				void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceClipHorizontalShift));

				if (consMemory) {
					ConsequenceClipHorizontalShift* newConsequence = new (consMemory)
					    ConsequenceClipHorizontalShift(shiftAmount, shiftAutomation, shiftSequenceAndMPE);
					action->addConsequence(newConsequence);
				}
			}
		}
		return ActionResult::DEALT_WITH;
	}

	// Or, if shift button not pressed...
	else {

		// If tempoless recording, don't allow
		if (!getCurrentClip()->currentlyScrollableAndZoomable()) {
			return ActionResult::DEALT_WITH;
		}

		// Otherwise, let parent do scrolling and zooming
		return ClipNavigationTimelineView::horizontalEncoderAction(offset);
	}
}
uint32_t ClipView::changeClipLength(int32_t offset, uint32_t oldLength, Action*& action) {
	bool rightOnSquare;
	uint32_t newLength;
	int32_t endSquare = getSquareFromPos(oldLength, &rightOnSquare);

	// Lengthening
	if (offset == 1) {

		newLength = getPosFromSquare(endSquare) + getLengthExtendAmount(endSquare);

		// If we're still within limits
		if (newLength <= (uint32_t)kMaxSequenceLength) {

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
					// editor.displayZoomLevel(true);
				}
				else {
					goto doReRender;
				}
			}
		}
	}
	return newLength;
}

int32_t ClipView::getLengthChopAmount(int32_t square) {

	square--; // We want the width of the square before
	while (!isSquareDefined(square)) {
		square--;
	}

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

	while (!isSquareDefined(square)) {
		square++;
	}

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

int32_t ClipView::getTickSquare() {

	int32_t newTickSquare = getSquareFromPos(getCurrentClip()->getLivePos());

	// See if we maybe want to do an auto-scroll
	if (getCurrentClip()->getCurrentlyRecordingLinearly()) {

		if (newTickSquare == kDisplayWidth && (!currentUIMode || currentUIMode == UI_MODE_AUDITIONING)
		    && getCurrentUI() == this && // currentPlaybackMode == &session &&
		    (getCurrentClip()->armState == ArmState::OFF || xScrollBeforeFollowingAutoExtendingLinearRecording != -1)) {

			if (xScrollBeforeFollowingAutoExtendingLinearRecording == -1) {
				xScrollBeforeFollowingAutoExtendingLinearRecording = currentSong->xScroll[NAVIGATION_CLIP];
			}

			int32_t newXScroll =
			    currentSong->xScroll[NAVIGATION_CLIP] + currentSong->xZoom[NAVIGATION_CLIP] * kDisplayWidth;

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
