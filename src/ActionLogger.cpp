/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include <ActionClipState.h>
#include <ArrangerView.h>
#include <AudioEngine.h>
#include <InstrumentClipMinder.h>
#include <InstrumentClipView.h>
#include "ActionLogger.h"
#include "definitions.h"
#include "Action.h"
#include "KeyboardScreen.h"
#include "functions.h"
#include "numericdriver.h"
#include "View.h"
#include "ConsequenceSwingChange.h"
#include "ConsequenceTempoChange.h"
#include "ConsequenceParamChange.h"
#include "kit.h"
#include <new>
#include "uart.h"
#include <string.h>
#include "GeneralMemoryAllocator.h"
#include "PlaybackMode.h"
#include "song.h"
#include "AudioClipView.h"
#include "Arrangement.h"
#include "Session.h"
#include "ConsequenceClipBeginLinearRecord.h"
#include "ConsequenceNoteArrayChange.h"
#include "IndicatorLEDs.h"
#include "SessionView.h"
#include "InstrumentClip.h"
#include "playbackhandler.h"
#include "oled.h"

ActionLogger actionLogger{};

ActionLogger::ActionLogger() {
	firstAction[BEFORE] = NULL;
	firstAction[AFTER] = NULL;
}

void ActionLogger::deleteLastActionIfEmpty() {
	if (firstAction[BEFORE]) {

		// There are probably more cases where we might want to do this, but I've only done it for recording so far
		if (firstAction[BEFORE]->type == ACTION_RECORD && !firstAction[BEFORE]->firstConsequence) {

			deleteLastAction();
		}
	}
}

void ActionLogger::deleteLastAction() {
	Action* toDelete = firstAction[BEFORE];

	firstAction[BEFORE] = firstAction[BEFORE]->nextAction;

	toDelete->prepareForDestruction(BEFORE, currentSong);
	toDelete->~Action();
	generalMemoryAllocator.dealloc(toDelete);
}

Action* ActionLogger::getNewAction(int newActionType, int addToExistingIfPossible) {

	deleteLog(AFTER);

	// If not on a View, not allowed!
	if (getCurrentUI() != getRootUI()) return NULL;

	Action* newAction;

	// If recording arrangement...
	if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
		return NULL;

		// If there's no action for that, we're really screwed, we'd better get out
		if (!firstAction[BEFORE] || firstAction[BEFORE]->type != ACTION_ARRANGEMENT_RECORD) return NULL;

		// Only a couple of kinds of new actions are allowed to add to that action
		if (newActionType == ACTION_SWING_CHANGE || newActionType == ACTION_TEMPO_CHANGE) {
			newAction = firstAction[BEFORE];
		}

		// Otherwise, not allowed
		else return NULL;
	}

	// See if we can add to an existing action...
	else if (addToExistingIfPossible && firstAction[BEFORE] && firstAction[BEFORE]->openForAdditions
	         && firstAction[BEFORE]->type == newActionType && firstAction[BEFORE]->view == getCurrentUI()
	         && (addToExistingIfPossible == ACTION_ADDITION_ALLOWED
	             || firstAction[BEFORE]->creationTime == AudioEngine::audioSampleTimer)) {
		newAction = firstAction[BEFORE];
	}

	// If we can't do that...
	else {

		deleteLastActionIfEmpty();

		// Make sure we close off any existing action
		if (firstAction[BEFORE]) firstAction[BEFORE]->openForAdditions = false;

		// And make a new one
		void* actionMemory = generalMemoryAllocator.alloc(sizeof(Action), NULL, true);

		if (!actionMemory) {
			Uart::println("no ram to create new Action");
			return NULL;
		}

		// Store states of every Clip in existence
		int numClips = currentSong->sessionClips.getNumElements() + currentSong->arrangementOnlyClips.getNumElements();

		ActionClipState* clipStates =
		    (ActionClipState*)generalMemoryAllocator.alloc(numClips * sizeof(ActionClipState), NULL, true);

		if (!clipStates) {
			generalMemoryAllocator.dealloc(actionMemory);
			return NULL;
		}

		newAction = new (actionMemory) Action(newActionType);
		newAction->clipStates = clipStates;

		int i = 0;

		// For each Clip in session and arranger
		ClipArray* clipArray = &currentSong->sessionClips;
traverseClips:
		for (int c = 0; c < clipArray->getNumElements(); c++) {
			Clip* clip = clipArray->getClipAtIndex(c);

			newAction->clipStates[i].grabFromClip(clip);
			i++;
		}
		if (clipArray != &currentSong->arrangementOnlyClips) {
			clipArray = &currentSong->arrangementOnlyClips;
			goto traverseClips;
		}

		newAction->numClipStates = numClips;

		// Only now put the new action into the list of undo actions - because in the above steps, we may have decided to delete it and get out (if we ran out of RAM while creating the ActionClipStates)
		newAction->nextAction = firstAction[BEFORE];
		firstAction[BEFORE] = newAction;

		// And fill out all the snapshot stuff that the Action captures at a song-wide level
		newAction->yScrollSongView[BEFORE] = currentSong->getYScrollSongViewWithoutPendingOverdubs();
		newAction->xScrollClip[BEFORE] = currentSong->xScroll[NAVIGATION_CLIP];
		newAction->xZoomClip[BEFORE] = currentSong->xZoom[NAVIGATION_CLIP];

		newAction->yScrollArranger[BEFORE] = currentSong->arrangementYScroll;
		newAction->xScrollArranger[BEFORE] = currentSong->xScroll[NAVIGATION_ARRANGEMENT];
		newAction->xZoomArranger[BEFORE] = currentSong->xZoom[NAVIGATION_ARRANGEMENT];

		newAction->numModeNotes[BEFORE] = currentSong->numModeNotes;
		memcpy(newAction->modeNotes[BEFORE], currentSong->modeNotes, sizeof(currentSong->modeNotes));

		newAction->tripletsOn = currentSong->tripletsOn;
		newAction->tripletsLevel = currentSong->tripletsLevel;
		//newAction->modKnobModeSongView = currentSong->modKnobMode;
		newAction->affectEntireSongView = currentSong->affectEntire;

		newAction->view = getCurrentUI();
		newAction->currentClip = currentSong->currentClip;
	}

	updateAction(newAction);

	return newAction;
}

void ActionLogger::updateAction(Action* newAction) {
	// Update ActionClipStates for each Clip
	if (newAction->numClipStates) {

		// If number of Clips has changed, discard
		if (newAction->numClipStates
		    != currentSong->sessionClips.getNumElements() + currentSong->arrangementOnlyClips.getNumElements()) {
			newAction->numClipStates = 0;
			generalMemoryAllocator.dealloc(newAction->clipStates);
			newAction->clipStates = NULL;
			Uart::println("discarded clip states");
		}

		else {
			int i = 0;

			// For each Clip in session and arranger
			ClipArray* clipArray = &currentSong->sessionClips;
traverseClips2:
			for (int c = 0; c < clipArray->getNumElements(); c++) {
				Clip* clip = clipArray->getClipAtIndex(c);

				if (clip->type == CLIP_TYPE_INSTRUMENT) {
					newAction->clipStates[i].yScrollSessionView[AFTER] = ((InstrumentClip*)clip)->yScroll;
				}
				i++;
			}
			if (clipArray != &currentSong->arrangementOnlyClips) {
				clipArray = &currentSong->arrangementOnlyClips;
				goto traverseClips2;
			}
		}
	}

	newAction->yScrollSongView[AFTER] = currentSong->getYScrollSongViewWithoutPendingOverdubs();
	newAction->xScrollClip[AFTER] = currentSong->xScroll[NAVIGATION_CLIP];
	newAction->xZoomClip[AFTER] = currentSong->xZoom[NAVIGATION_CLIP];

	newAction->yScrollArranger[AFTER] = currentSong->arrangementYScroll;
	newAction->xScrollArranger[AFTER] = currentSong->xScroll[NAVIGATION_ARRANGEMENT];
	newAction->xZoomArranger[AFTER] = currentSong->xZoom[NAVIGATION_ARRANGEMENT];

	newAction->numModeNotes[AFTER] = currentSong->numModeNotes;
	memcpy(newAction->modeNotes[AFTER], currentSong->modeNotes, sizeof(currentSong->modeNotes));
}

void ActionLogger::recordUnautomatedParamChange(ModelStackWithAutoParam const* modelStack, int actionType) {

	Action* action = getNewAction(actionType, true);
	if (!action) return;

	action->recordParamChangeIfNotAlreadySnapshotted(modelStack, false);
}

void ActionLogger::recordSwingChange(int8_t swingBefore, int8_t swingAfter) {

	Action* action = getNewAction(ACTION_SWING_CHANGE, true);
	if (!action) return;

	// See if there's a previous one we can update
	if (action->firstConsequence) {
		ConsequenceSwingChange* consequence = (ConsequenceSwingChange*)action->firstConsequence;
		consequence->swing[AFTER] = swingAfter;
	}
	else {
		void* consMemory = generalMemoryAllocator.alloc(sizeof(ConsequenceSwingChange));

		if (consMemory) {
			ConsequenceSwingChange* newConsequence = new (consMemory) ConsequenceSwingChange(swingBefore, swingAfter);
			action->addConsequence(newConsequence);
		}
	}
}

void ActionLogger::recordTempoChange(uint64_t timePerBigBefore, uint64_t timePerBigAfter) {

	Action* action = getNewAction(ACTION_TEMPO_CHANGE, true);
	if (!action) return;

	// See if there's a previous one we can update
	if (action->firstConsequence) {
		ConsequenceTempoChange* consequence = (ConsequenceTempoChange*)action->firstConsequence;
		consequence->timePerBig[AFTER] = timePerBigAfter;
	}
	else {

		void* consMemory = generalMemoryAllocator.alloc(sizeof(ConsequenceTempoChange));

		if (consMemory) {
			ConsequenceTempoChange* newConsequence =
			    new (consMemory) ConsequenceTempoChange(timePerBigBefore, timePerBigAfter);
			action->addConsequence(newConsequence);
		}
	}
}

// Returns whether anything was reverted.
// doNavigation and updateVisually are only false when doing one of those undo-Clip-resize things as part of another Clip resize.
// You must not call this during the card routine - though I've lost track of the exact reason why not - is it just because we could then be in the middle of executing whichever function accessed the card and we don't know if things will break?
bool ActionLogger::revert(int time, bool updateVisually, bool doNavigation) {
	Uart::println("ActionLogger::revert");

	deleteLastActionIfEmpty();

	if (firstAction[time]) {
		Action* toRevert = firstAction[time];

		// If we're in a UI mode, and reverting this Action would mean changing UI, we have to disallow that.
		if (toRevert->view != getCurrentUI() && !isNoUIModeActive()) {
			return false;
		}

		firstAction[time] = firstAction[time]->nextAction;

		revertAction(toRevert, updateVisually, doNavigation, time);

		toRevert->nextAction = firstAction[1 - time];
		firstAction[1 - time] = toRevert;
		return true;
	}

	return false;
}

#define ANIMATION_NONE 0
#define ANIMATION_SCROLL 1
#define ANIMATION_ZOOM 2
#define ANIMATION_CLIP_MINDER_TO_SESSION 3
#define ANIMATION_SESSION_TO_CLIP_MINDER 4
#define ANIMATION_ENTER_KEYBOARD_VIEW 5
#define ANIMATION_EXIT_KEYBOARD_VIEW 6
#define ANIMATION_CHANGE_CLIP 7
#define ANIMATION_CLIP_MINDER_TO_ARRANGEMENT 8
#define ANIMATION_ARRANGEMENT_TO_CLIP_MINDER 9
#define ANIMATION_SESSION_TO_ARRANGEMENT 10
#define ANIMATION_ARRANGEMENT_TO_SESSION 11

// doNavigation and updateVisually are only false when doing one of those undo-Clip-resize things as part of another Clip resize
void ActionLogger::revertAction(Action* action, bool updateVisually, bool doNavigation, int time) {

	currentSong->deletePendingOverdubs();

	int whichAnimation = ANIMATION_NONE;
	uint32_t songZoomBeforeTransition = currentSong->xZoom[NAVIGATION_CLIP];
	uint32_t arrangerZoomBeforeTransition = currentSong->xZoom[NAVIGATION_ARRANGEMENT];

	if (doNavigation) {

		// If it's an arrangement record action...
		if (action->type == ACTION_ARRANGEMENT_RECORD) {

			// If user is in song view or arranger view, just stay in that UI.
			if (getCurrentUI() == &arrangerView || getCurrentUI() == &sessionView) {
				action->view = getCurrentUI();

				// If in arranger view, don't go scrolling anywhere - that'd just visually confuse things
				if (getCurrentUI() == &arrangerView) {
					action->xScrollArranger[time] = currentSong->xScroll[NAVIGATION_ARRANGEMENT];
				}
			}
		}

		// We only want to display one animation

		if (updateVisually) {

			// Switching between session / arranger
			if (action->view == &sessionView && getCurrentUI() == &arrangerView) {
				whichAnimation = ANIMATION_ARRANGEMENT_TO_SESSION;
			}
			else if (action->view == &arrangerView && getCurrentUI() == &sessionView) {
				whichAnimation = ANIMATION_SESSION_TO_ARRANGEMENT;
			}

			// Switching between session and clip view
			else if (action->view == &sessionView && getCurrentUI()->toClipMinder()) {
				whichAnimation = ANIMATION_CLIP_MINDER_TO_SESSION;
			}
			else if (action->view->toClipMinder() && getCurrentUI() == &sessionView) {
				whichAnimation = ANIMATION_SESSION_TO_CLIP_MINDER;
			}

			// Entering / exiting arranger
			else if (action->view == &arrangerView && getCurrentUI()->toClipMinder()) {
				whichAnimation = ANIMATION_CLIP_MINDER_TO_ARRANGEMENT;
			}
			else if (action->view->toClipMinder() && getCurrentUI() == &arrangerView) {
				whichAnimation = ANIMATION_ARRANGEMENT_TO_CLIP_MINDER;
			}

			// Then entering or exiting keyboard view
			else if (action->view == &keyboardScreen && getCurrentUI() != &keyboardScreen) {
				whichAnimation = ANIMATION_ENTER_KEYBOARD_VIEW;
			}
			else if (action->view != &keyboardScreen && getCurrentUI() == &keyboardScreen) {
				whichAnimation = ANIMATION_EXIT_KEYBOARD_VIEW;
			}

			// Or if we've changed Clip but ended up back in the same view...
			else if (getCurrentUI()->toClipMinder() && currentSong->currentClip != action->currentClip) {
				whichAnimation = ANIMATION_CHANGE_CLIP;
			}

			// Or if none of those is happening, we might like to do a horizontal zoom or scroll - only if [vertical scroll isn't changed], and we're not on keyboard view
			else {
				if (getCurrentUI() != &keyboardScreen) {

					if (getCurrentUI() == &arrangerView) {
						if (currentSong->xZoom[NAVIGATION_ARRANGEMENT] != action->xZoomArranger[time]) {
							whichAnimation = ANIMATION_ZOOM;
						}

						else if (currentSong->xScroll[NAVIGATION_ARRANGEMENT] != action->xScrollArranger[time]) {
							whichAnimation = ANIMATION_SCROLL;
						}
					}

					else {
						if (currentSong->xZoom[NAVIGATION_CLIP] != action->xZoomClip[time]) {
							whichAnimation = ANIMATION_ZOOM;
						}

						else if (currentSong->xScroll[NAVIGATION_CLIP] != action->xScrollClip[time]) {
							whichAnimation = ANIMATION_SCROLL;
						}
					}
				}
			}
		}

		// Change some stuff that'll need to get changed in any case
		currentSong->xZoom[NAVIGATION_CLIP] = action->xZoomClip[time];
		currentSong->xZoom[NAVIGATION_ARRANGEMENT] = action->xZoomArranger[time];

		// Restore states of each Clip
		if (action->numClipStates) {
			int totalNumClips =
			    currentSong->sessionClips.getNumElements() + currentSong->arrangementOnlyClips.getNumElements();
			if (action->numClipStates == totalNumClips) {

				int i = 0;

				// For each Clip in session and arranger
				ClipArray* clipArray = &currentSong->sessionClips;
traverseClips:
				for (int c = 0; c < clipArray->getNumElements(); c++) {
					Clip* clip = clipArray->getClipAtIndex(c);

					//clip->modKnobMode = action->clipStates[i].modKnobMode;

					if (clip->type == CLIP_TYPE_INSTRUMENT) {
						InstrumentClip* instrumentClip = (InstrumentClip*)clip;
						instrumentClip->yScroll = action->clipStates[i].yScrollSessionView[time];
						instrumentClip->affectEntire = action->clipStates[i].affectEntire;
						instrumentClip->wrapEditing = action->clipStates[i].wrapEditing;
						instrumentClip->wrapEditLevel = action->clipStates[i].wrapEditLevel;

						if (clip->output->type == INSTRUMENT_TYPE_KIT) {
							Kit* kit = (Kit*)clip->output;
							if (action->clipStates[i].selectedDrumIndex == -1) kit->selectedDrum = NULL;
							else kit->selectedDrum = kit->getDrumFromIndex(action->clipStates[i].selectedDrumIndex);
						}
					}

					i++;
				}
				if (clipArray != &currentSong->arrangementOnlyClips) {
					clipArray = &currentSong->arrangementOnlyClips;
					goto traverseClips;
				}
			}
			else {
				Uart::println("clip states wrong number so not restoring");
			}
		}

		// Vertical scroll
		currentSong->songViewYScroll = action->yScrollSongView[time];
		currentSong->arrangementYScroll = action->yScrollArranger[time];

		// Musical Scale
		currentSong->numModeNotes = action->numModeNotes[time];
		memcpy(currentSong->modeNotes, action->modeNotes[time], sizeof(currentSong->modeNotes));

		// Other stuff
		//currentSong->modKnobMode = action->modKnobModeSongView;
		currentSong->affectEntire = action->affectEntireSongView;
		currentSong->tripletsOn = action->tripletsOn;
		currentSong->tripletsLevel = action->tripletsLevel;

		// Now do the animation we decided on - for animations which we prefer to set up before reverting the actual action
		if (whichAnimation == ANIMATION_SCROLL && getCurrentUI() == &arrangerView) {
			bool worthDoingAnimation = arrangerView.initiateXScroll(action->xScrollArranger[time]);
			if (!worthDoingAnimation) {
				whichAnimation = ANIMATION_NONE;
				goto otherOption;
			}
		}
		else {
otherOption:
			if (getCurrentUI() != &arrangerView || whichAnimation != ANIMATION_ZOOM) {
				currentSong->xScroll[NAVIGATION_ARRANGEMENT] =
				    action->xScrollArranger
				        [time]; // Have to do this if we didn't do the actual scroll animation yet some scrolling happened
			}
		}

		if (whichAnimation == ANIMATION_SCROLL && getCurrentUI() != &arrangerView) {
			((TimelineView*)getCurrentUI())->initiateXScroll(action->xScrollClip[time]);
		}
		else if (getCurrentUI() == &arrangerView || whichAnimation != ANIMATION_ZOOM)
			currentSong->xScroll[NAVIGATION_CLIP] =
			    action->xScrollClip
			        [time]; // Have to do this if we didn't do the actual scroll animation yet some scrolling happened

		if (whichAnimation == ANIMATION_ZOOM) {
			if (getCurrentUI() == &arrangerView)
				arrangerView.initiateXZoom(
				    howMuchMoreMagnitude(action->xZoomArranger[time], arrangerZoomBeforeTransition),
				    action->xScrollArranger[time], arrangerZoomBeforeTransition);
			else
				((TimelineView*)getCurrentUI())
				    ->initiateXZoom(howMuchMoreMagnitude(action->xZoomClip[time], songZoomBeforeTransition),
				                    action->xScrollClip[time], songZoomBeforeTransition);
		}

		else if (whichAnimation == ANIMATION_CLIP_MINDER_TO_SESSION) {
			if (getCurrentUI() == &audioClipView) {
				audioClipView.transitionToSessionView();
			}
			else {
				instrumentClipView.transitionToSessionView();
			}
		}

		else if (whichAnimation == ANIMATION_SESSION_TO_CLIP_MINDER) {
			sessionView.transitionToViewForClip(action->currentClip);
			goto currentClipSwitchedOver; // Skip the below - our call to transitionToViewForClip will switch it over for us
		}

		// Swap currentClip over. Can only do this after calling transitionToSessionView(). Previously, we did this much earlier,
		// causing a crash. Hopefully moving it later here is ok...
		if (action
		        ->currentClip) { // If song just loaded and we hadn't been into ClipMinder yet, this would be NULL, and we don't want to set currentSong->currentClip back to this
			currentSong->currentClip = action->currentClip;
		}
	}

currentClipSwitchedOver:

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	int error = action->revert(time, modelStack);

	// Some "animations", we prefer to do after we've reverted the action
	if (whichAnimation == ANIMATION_ENTER_KEYBOARD_VIEW) {
		changeRootUI(&keyboardScreen);
	}

	else if (whichAnimation == ANIMATION_EXIT_KEYBOARD_VIEW) {
		changeRootUI(&instrumentClipView);
	}

	else if (whichAnimation == ANIMATION_CHANGE_CLIP) {
		if (action->view != getCurrentUI()) changeRootUI(action->view);
		else {
			getCurrentUI()->focusRegained();
			renderingNeededRegardlessOfUI(); // Didn't have this til March 2020, and stuff didn't update. Guess this is just needed? Can't remember specifics just now
		}
	}

	else if (whichAnimation == ANIMATION_CLIP_MINDER_TO_ARRANGEMENT) {
		changeRootUI(&arrangerView);
	}

	else if (whichAnimation == ANIMATION_ARRANGEMENT_TO_CLIP_MINDER) {
		if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) changeRootUI(&audioClipView);
		else if (((InstrumentClip*)currentSong->currentClip)->onKeyboardScreen) changeRootUI(&keyboardScreen);
		else changeRootUI(&instrumentClipView);
	}

	else if (whichAnimation == ANIMATION_SESSION_TO_ARRANGEMENT) {
		changeRootUI(&arrangerView);
	}

	else if (whichAnimation == ANIMATION_ARRANGEMENT_TO_SESSION) {
		changeRootUI(&sessionView);
	}

	if (updateVisually) {

		if (getCurrentUI() == &instrumentClipView) {
			if (whichAnimation != ANIMATION_CLIP_MINDER_TO_SESSION
			    && whichAnimation
			           != ANIMATION_CLIP_MINDER_TO_ARRANGEMENT) { // If we're not animating away from this view (but something like scrolling sideways would be allowed)
				instrumentClipView.recalculateColours();
				if (!whichAnimation) {
					uiNeedsRendering(&instrumentClipView);
				}
			}
		}
		else if (getCurrentUI() == &audioClipView) {
			if (!whichAnimation) {
				uiNeedsRendering(&audioClipView);
			}
		}
		else if (getCurrentUI() == &keyboardScreen) {
			if (whichAnimation != ANIMATION_ENTER_KEYBOARD_VIEW) {
				uiNeedsRendering(&keyboardScreen, 0xFFFFFFFF, 0);
			}
		}
		else if (getCurrentUI() == &sessionView) {
			uiNeedsRendering(
			    &sessionView, 0xFFFFFFFF,
			    0xFFFFFFFF); // Got to try this even if we're supposedly doing a horizontal scroll animation or something cos that may have failed if the Clip wasn't long enough before we did the action->revert() ...
		}
		else if (getCurrentUI() == &arrangerView) {
			arrangerView.repopulateOutputsOnScreen(!whichAnimation);
		}

		// Usually need to re-display the mod LEDs etc, but not if either of these animations is happening, which means that it'll happen anyway
		// when the animation finishes - and also, if we just deleted the Clip which was the activeModControllableClip, well that'll temporarily be pointing to invalid stuff.
		// Check the actual UI mode rather than the whichAnimation variable we've been using in this function, because under some circumstances that'll bypass the actual
		// animation / UI-mode.
		if (!isUIModeActive(UI_MODE_AUDIO_CLIP_COLLAPSING)
		    && !isUIModeActive(
		        UI_MODE_INSTRUMENT_CLIP_COLLAPSING)) { // We would also put the "explode" animation for transitioning *to* arranger here, but it just doesn't get used during reversion.
			view.setKnobIndicatorLevels();
			view.setModLedStates();
		}

		switch (whichAnimation) { // So long as we're not gonna animate to different UI...
		case ANIMATION_CLIP_MINDER_TO_SESSION:
		case ANIMATION_SESSION_TO_CLIP_MINDER:
		case ANIMATION_CLIP_MINDER_TO_ARRANGEMENT:
		case ANIMATION_ARRANGEMENT_TO_CLIP_MINDER:
			break;

		default:
			ClipMinder* clipMinder = getCurrentUI()->toClipMinder();
			if (clipMinder) {
				if (currentSong->currentClip->type == CLIP_TYPE_INSTRUMENT) {
					((InstrumentClipMinder*)clipMinder)->setLedStates();
				}
			}
			else if (getCurrentUI() == &sessionView) {
				sessionView.setLedStates();
			}
			view.setTripletsLedState();
		}
	}

	if (playbackHandler.isEitherClockActive()) currentPlaybackMode->reversionDone(); // Re-gets automation and stuff

	// If there was an actual error in the reversion itself...
	if (error) {
		numericDriver.displayError(error);

		deleteAllLogs();
	}
}

void ActionLogger::closeAction(int actionType) {
	if (firstAction[BEFORE] && firstAction[BEFORE]->type == actionType) {
		firstAction[BEFORE]->openForAdditions = false;
	}
}

void ActionLogger::closeActionUnlessCreatedJustNow(int actionType) {
	if (firstAction[BEFORE] && firstAction[BEFORE]->type == actionType
	    && firstAction[BEFORE]->creationTime != AudioEngine::audioSampleTimer) {
		firstAction[BEFORE]->openForAdditions = false;
	}
}

void ActionLogger::deleteAllLogs() {
	deleteLog(BEFORE);
	deleteLog(AFTER);
}

void ActionLogger::deleteLog(int time) {
	while (firstAction[time]) {
		Action* toDelete = firstAction[time];

		firstAction[time] = firstAction[time]->nextAction;

		toDelete->prepareForDestruction(time, currentSong);
		toDelete->~Action();
		generalMemoryAllocator.dealloc(toDelete);
	}
}

// You must not call this during the card routine - though I've lost track of the exact reason why not - is it just because we could then be in the middle of executing whichever function accessed the card and we don't know if things will break?
void ActionLogger::undo() {

	// Before we go and revert the most recent Action, there are a few recording-related states we first want to have a go at cancelling out of.
	// These are treated as special cases here rather than being Consequences because they're never redoable: their "undoing" is a special case of cancellation.

	// But, this is to be done very sparingly! I used to have more of these which did things like deleting Clips for which linear recording was ongoing.
	// But then what if other Consequences, e.g. param automation, had been recorded for those? Reverting those would call functions on invalid pointers.
	// So instead, do just use regular Actions and Consequences for everything possible. And definitely don't delete any Clips here.

	// If currently recording an arrangement from session, we have to stop doing so first
	if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
		playbackHandler.recording = RECORDING_OFF;
		currentSong->resumeClipsClonedForArrangementRecording();

		view.setModLedStates(); // Set song LED back
		playbackHandler.setLedStates();
	}

	// Or if recording tempoless, gotta stop that
	else if (playbackHandler.playbackState && !playbackHandler.isEitherClockActive()) {
		playbackHandler.endPlayback();
		goto displayUndoMessage;
	}

	// Or if recording linearly to arrangement, gotta exit that mode
	else if (playbackHandler.playbackState && playbackHandler.recording && currentPlaybackMode == &arrangement) {
		arrangement.endAnyLinearRecording();
	}

	// Ok, do the actual undo.
	if (revert(BEFORE)) {
displayUndoMessage:
#ifdef undoLedX
		IndicatorLEDs::indicateAlertOnLed(undoLedX, undoLedY);
#elif HAVE_OLED
		OLED::consoleText("Undo");
#else
		numericDriver.displayPopup("UNDO");
#endif
	}
}

// You must not call this during the card routine - though I've lost track of the exact reason why not - is it just because we could then be in the middle of executing whichever function accessed the card and we don't know if things will break?
void ActionLogger::redo() {
	if (revert(AFTER)) {
#ifdef redoLedX
		IndicatorLEDs::indicateAlertOnLed(redoLedX, redoLedY);
#elif HAVE_OLED
		OLED::consoleText("Redo");
#else
		numericDriver.displayPopup("REDO");
#endif
	}
}

const uint32_t reversionUIModes[] = {UI_MODE_AUDITIONING, UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION,
                                     UI_MODE_CLIP_PRESSED_IN_SONG_VIEW, UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON, 0};

bool ActionLogger::allowedToDoReversion() {
	return (currentSong && getCurrentUI() == getRootUI() && isUIModeWithinRange(reversionUIModes));
}

void ActionLogger::notifyClipRecordingAborted(Clip* clip) {

	// If there's an Action which only recorded the beginning of this Clip recording, we don't want it anymore.
	if (firstAction[BEFORE] && firstAction[BEFORE]->type == ACTION_RECORD) {
		Consequence* firstConsequence = firstAction[BEFORE]->firstConsequence;
		if (!firstConsequence->next && firstConsequence->type == CONSEQUENCE_CLIP_BEGIN_LINEAR_RECORD) {
			if (clip == ((ConsequenceClipBeginLinearRecord*)firstConsequence)->clip) {
				deleteLastAction();
			}
		}
	}
}

// This function relies on Consequences having been sequentially added for each subsequent "mini action", so looking at the noteRowId of the most recent one,
// we can then know that all further Consequences until we see the same noteRowId again are part of the same "mini action".
// This will get called in some cases (Action types) were only one NoteRow, not many, could have had the editing done to it: that's fine too, and barely any time is really
// wasted here.
// Returns whether whole Action was reverted, which is the only case where visual updating / rendering, and also the calling of expectEvent(), would have taken place
bool ActionLogger::undoJustOneConsequencePerNoteRow(ModelStack* modelStack) {

	bool revertedWholeAction = false;

	Consequence* firstConsequence = firstAction[BEFORE]->firstConsequence;
	if (firstConsequence) { // Should always be true

		// Work out if multiple Consequences per NoteRow (see big comment above)
		int firstNoteRowId = ((ConsequenceNoteArrayChange*)firstConsequence)->noteRowId;

		Consequence* thisConsequence = firstConsequence->next;
		while (thisConsequence) {
			if (thisConsequence->type == CONSEQUENCE_NOTE_ARRAY_CHANGE
			    && ((ConsequenceNoteArrayChange*)thisConsequence)->noteRowId == firstNoteRowId) {
				goto gotMultipleConsequencesPerNoteRow;
			}

			thisConsequence = thisConsequence->next;
		}

		// If multiple Consequences per NoteRow, just revert most recent one per NoteRow
		if (false) {
gotMultipleConsequencesPerNoteRow:
			do {
				firstConsequence->revert(
				    BEFORE,
				    modelStack); // Unlike reverting a whole Action, this doesn't update anything visually or call any expectEvent() functions
				firstAction[BEFORE]->firstConsequence = firstConsequence->next;

				firstConsequence->prepareForDestruction(BEFORE, modelStack->song);
				firstConsequence->~Consequence();
				generalMemoryAllocator.dealloc(firstConsequence);
				firstConsequence = firstAction[BEFORE]->firstConsequence;
			} while (thisConsequence->type != CONSEQUENCE_NOTE_ARRAY_CHANGE
			         || ((ConsequenceNoteArrayChange*)firstConsequence)->noteRowId != firstNoteRowId);

			Uart::println("did secret undo, just one Consequence");
		}

		// Or if only one Consequence (per NoteRow), revert whole Action
		else {
			revert(BEFORE, true, false);
			Uart::println("did secret undo, whole Action");
			revertedWholeAction = true;
		}

		deleteLog(AFTER);
	}

	return revertedWholeAction;
}
