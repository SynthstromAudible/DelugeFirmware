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

#include "model/action/action_logger.h"
#include "definitions_cxx.hpp"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_pattern_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_clip_state.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/consequence/consequence_clip_begin_linear_record.h"
#include "model/consequence/consequence_note_array_change.h"
#include "model/consequence/consequence_param_change.h"
#include "model/consequence/consequence_performance_view_press.h"
#include "model/consequence/consequence_swing_change.h"
#include "model/consequence/consequence_tempo_change.h"
#include "model/instrument/kit.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/playback_mode.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"
#include <new>
#include <string.h>

ActionLogger actionLogger{};

ActionLogger::ActionLogger() {
	firstAction[BEFORE] = NULL;
	firstAction[AFTER] = NULL;
}

void ActionLogger::deleteLastActionIfEmpty() {
	if (firstAction[BEFORE]) {

		// There are probably more cases where we might want to do this, but I've only done it for recording so far
		// Paul: reinstating the original for now because it seems there are broken pointers in this list which lead to
		// crashes, we need to fix after release while (!firstAction[BEFORE]->firstConsequence) {
		if (firstAction[BEFORE]->type == ActionType::RECORD && !firstAction[BEFORE]->firstConsequence) {

			deleteLastAction();
		}
	}
}

void ActionLogger::deleteLastAction() {
	Action* toDelete = firstAction[BEFORE];

	firstAction[BEFORE] = firstAction[BEFORE]->nextAction;

	toDelete->prepareForDestruction(BEFORE, currentSong);
	toDelete->~Action();
	delugeDealloc(toDelete);
}

Action* ActionLogger::getNewAction(ActionType newActionType, ActionAddition addToExistingIfPossible) {

	if (!currentSong) {
		return nullptr;
	}
	deleteLog(AFTER);

	// If not on a View, not allowed!
	// Exception for sound editor note editor UI which can edit notes on the grid
	// Exception for sound editor note row editor UI which can edit note rows on the grid
	// Exception for loadPatternUI which does edit note rows on the grid
	if ((getCurrentUI() != getRootUI())
	    && (!(getCurrentUI() == &soundEditor && (soundEditor.inNoteEditor() || soundEditor.inNoteRowEditor())))
	    && (getCurrentUI() != &loadPatternUI)) {
		return nullptr;
	}

	Action* newAction;

	// If recording arrangement...
	if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
		return NULL;

		// If there's no action for that, we're really screwed, we'd better get out
		if (!firstAction[BEFORE] || firstAction[BEFORE]->type != ActionType::ARRANGEMENT_RECORD) {
			return NULL;
		}

		// Only a couple of kinds of new actions are allowed to add to that action
		if (newActionType == ActionType::SWING_CHANGE || newActionType == ActionType::TEMPO_CHANGE) {
			newAction = firstAction[BEFORE];
		}

		// Otherwise, not allowed
		else {
			return NULL;
		}
	}

	// See if we can add to an existing action...
	else if (addToExistingIfPossible != ActionAddition::NOT_ALLOWED && firstAction[BEFORE]
	         && firstAction[BEFORE]->openForAdditions && firstAction[BEFORE]->type == newActionType
	         && firstAction[BEFORE]->view == getCurrentUI()
	         && (addToExistingIfPossible == ActionAddition::ALLOWED
	             || firstAction[BEFORE]->creationTime == AudioEngine::audioSampleTimer)) {
		newAction = firstAction[BEFORE];
	}

	// If we can't do that...
	else {

		deleteLastActionIfEmpty();

		// Make sure we close off any existing action
		if (firstAction[BEFORE]) {
			firstAction[BEFORE]->openForAdditions = false;
		}

		// And make a new one
		void* actionMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(Action));

		if (!actionMemory) {
			D_PRINTLN("no ram to create new Action");
			return NULL;
		}

		// Store states of every Clip in existence
		int32_t numClips =
		    currentSong->sessionClips.getNumElements() + currentSong->arrangementOnlyClips.getNumElements();

		ActionClipState* clipStates =
		    (ActionClipState*)GeneralMemoryAllocator::get().allocLowSpeed(numClips * sizeof(ActionClipState));

		if (!clipStates) {
			delugeDealloc(actionMemory);
			return NULL;
		}

		newAction = new (actionMemory) Action(newActionType);
		newAction->clipStates = clipStates;

		int32_t i = 0;
		for (Clip* clip : AllClips::everywhere(currentSong)) {
			newAction->clipStates[i++].grabFromClip(clip);
		}

		newAction->numClipStates = numClips;

		// Only now put the new action into the list of undo actions - because in the above steps, we may have decided
		// to delete it and get out (if we ran out of RAM while creating the ActionClipStates)
		newAction->nextAction = firstAction[BEFORE];
		firstAction[BEFORE] = newAction;

		// And fill out all the snapshot stuff that the Action captures at a song-wide level
		newAction->yScrollSongView[BEFORE] = currentSong->getYScrollSongViewWithoutPendingOverdubs();
		newAction->xScrollClip[BEFORE] = currentSong->xScroll[NAVIGATION_CLIP];
		newAction->xZoomClip[BEFORE] = currentSong->xZoom[NAVIGATION_CLIP];

		newAction->yScrollArranger[BEFORE] = currentSong->arrangementYScroll;
		newAction->xScrollArranger[BEFORE] = currentSong->xScroll[NAVIGATION_ARRANGEMENT];
		newAction->xZoomArranger[BEFORE] = currentSong->xZoom[NAVIGATION_ARRANGEMENT];

		newAction->modeNotes[BEFORE] = currentSong->key.modeNotes;

		newAction->tripletsOn = currentSong->tripletsOn;
		newAction->tripletsLevel = currentSong->tripletsLevel;
		// newAction->modKnobModeSongView = currentSong->modKnobMode;
		newAction->affectEntireSongView = currentSong->affectEntire;

		newAction->view = getCurrentUI();
		newAction->currentClip = getCurrentClip();
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
			delugeDealloc(newAction->clipStates);
			newAction->clipStates = NULL;
			D_PRINTLN("discarded clip states");
		}

		else {
			// NOTE: i ranges over all clips, not just instrument clips
			int32_t i = 0;
			for (Clip* clip : AllClips::everywhere(currentSong)) {
				if (clip->type == ClipType::INSTRUMENT) {
					newAction->clipStates[i].yScrollSessionView[AFTER] = ((InstrumentClip*)clip)->yScroll;
				}
				i++;
			}
		}
	}

	newAction->yScrollSongView[AFTER] = currentSong->getYScrollSongViewWithoutPendingOverdubs();
	newAction->xScrollClip[AFTER] = currentSong->xScroll[NAVIGATION_CLIP];
	newAction->xZoomClip[AFTER] = currentSong->xZoom[NAVIGATION_CLIP];

	newAction->yScrollArranger[AFTER] = currentSong->arrangementYScroll;
	newAction->xScrollArranger[AFTER] = currentSong->xScroll[NAVIGATION_ARRANGEMENT];
	newAction->xZoomArranger[AFTER] = currentSong->xZoom[NAVIGATION_ARRANGEMENT];

	newAction->modeNotes[AFTER] = currentSong->key.modeNotes;
}

void ActionLogger::recordUnautomatedParamChange(ModelStackWithAutoParam const* modelStack, ActionType actionType) {

	Action* action = getNewAction(actionType, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}

	action->recordParamChangeIfNotAlreadySnapshotted(modelStack, false);
}

void ActionLogger::recordSwingChange(int8_t swingBefore, int8_t swingAfter) {

	Action* action = getNewAction(ActionType::SWING_CHANGE, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}

	// See if there's a previous one we can update
	if (action->firstConsequence) {
		ConsequenceSwingChange* consequence = (ConsequenceSwingChange*)action->firstConsequence;
		consequence->swing[AFTER] = swingAfter;
	}
	else {
		void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceSwingChange));

		if (consMemory) {
			ConsequenceSwingChange* newConsequence = new (consMemory) ConsequenceSwingChange(swingBefore, swingAfter);
			action->addConsequence(newConsequence);
		}
	}
}

void ActionLogger::recordTempoChange(uint64_t timePerBigBefore, uint64_t timePerBigAfter) {

	Action* action = getNewAction(ActionType::TEMPO_CHANGE, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}

	// See if there's a previous one we can update
	if (action->firstConsequence) {
		ConsequenceTempoChange* consequence = (ConsequenceTempoChange*)action->firstConsequence;
		consequence->timePerBig[AFTER] = timePerBigAfter;
	}
	else {

		void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceTempoChange));

		if (consMemory) {
			ConsequenceTempoChange* newConsequence =
			    new (consMemory) ConsequenceTempoChange(timePerBigBefore, timePerBigAfter);
			action->addConsequence(newConsequence);
		}
	}
}

/// Record Performance View Hold Press
void ActionLogger::recordPerformanceViewPress(FXColumnPress fxPressBefore[kDisplayWidth],
                                              FXColumnPress fxPressAfter[kDisplayWidth], int32_t xDisplay) {

	Action* action = getNewAction(ActionType::PARAM_UNAUTOMATED_VALUE_CHANGE, ActionAddition::ALLOWED);

	if (!action) {
		return;
	}

	void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequencePerformanceViewPress));

	if (consMemory) {
		ConsequencePerformanceViewPress* newConsequence =
		    new (consMemory) ConsequencePerformanceViewPress(fxPressBefore, fxPressAfter, xDisplay);
		action->addConsequence(newConsequence);
	}
}

// Returns whether anything was reverted.
// doNavigation and updateVisually are only false when doing one of those undo-Clip-resize things as part of another
// Clip resize. You must not call this during the card routine - though I've lost track of the exact reason why not - is
// it just because we could then be in the middle of executing whichever function accessed the card and we don't know if
// things will break?
bool ActionLogger::revert(TimeType time, bool updateVisually, bool doNavigation) {
	D_PRINTLN("ActionLogger::revert");

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

enum class Animation {
	NONE,
	SCROLL,
	ZOOM,
	CLIP_MINDER_TO_SESSION,
	SESSION_TO_CLIP_MINDER,
	ENTER_KEYBOARD_VIEW,
	EXIT_KEYBOARD_VIEW,
	CHANGE_CLIP,
	CLIP_MINDER_TO_ARRANGEMENT,
	ARRANGEMENT_TO_CLIP_MINDER,
	SESSION_TO_ARRANGEMENT,
	ARRANGEMENT_TO_SESSION,
	ENTER_AUTOMATION_VIEW,
	EXIT_AUTOMATION_VIEW,
};

// doNavigation and updateVisually are only false when doing one of those undo-Clip-resize things as part of another
// Clip resize
void ActionLogger::revertAction(Action* action, bool updateVisually, bool doNavigation, TimeType time) {

	currentSong->deletePendingOverdubs();

	Animation whichAnimation = Animation::NONE;
	uint32_t songZoomBeforeTransition = currentSong->xZoom[NAVIGATION_CLIP];
	uint32_t arrangerZoomBeforeTransition = currentSong->xZoom[NAVIGATION_ARRANGEMENT];

	if (doNavigation) {

		// If it's an arrangement record action...
		if (action->type == ActionType::ARRANGEMENT_RECORD) {

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
				whichAnimation = Animation::ARRANGEMENT_TO_SESSION;
			}
			else if (action->view == &arrangerView && getCurrentUI() == &sessionView) {
				whichAnimation = Animation::SESSION_TO_ARRANGEMENT;
			}

			// Switching between session and clip view
			else if (action->view == &sessionView && getCurrentUI()->toClipMinder()) {
				whichAnimation = Animation::CLIP_MINDER_TO_SESSION;
			}
			else if (action->view->toClipMinder() && getCurrentUI() == &sessionView) {
				whichAnimation = Animation::SESSION_TO_CLIP_MINDER;
			}

			// Entering / exiting arranger
			else if (action->view == &arrangerView && getCurrentUI()->toClipMinder()) {
				whichAnimation = Animation::CLIP_MINDER_TO_ARRANGEMENT;
			}
			else if (action->view->toClipMinder() && getCurrentUI() == &arrangerView) {
				whichAnimation = Animation::ARRANGEMENT_TO_CLIP_MINDER;
			}

			// Then entering or exiting keyboard view
			else if (action->view == &keyboardScreen && getCurrentUI() != &keyboardScreen) {
				whichAnimation = Animation::ENTER_KEYBOARD_VIEW;
			}
			else if (action->view != &keyboardScreen && getCurrentUI() == &keyboardScreen) {
				whichAnimation = Animation::EXIT_KEYBOARD_VIEW;
			}

			// Then entering or exiting automation view
			else if (action->view == &automationView && getCurrentUI() != &automationView) {
				whichAnimation = Animation::ENTER_AUTOMATION_VIEW;
			}
			else if (action->view != &automationView && getCurrentUI() == &automationView) {
				whichAnimation = Animation::EXIT_AUTOMATION_VIEW;
			}

			// Or if we've changed Clip but ended up back in the same view...
			else if (getCurrentUI()->toClipMinder() && getCurrentClip() != action->currentClip) {
				whichAnimation = Animation::CHANGE_CLIP;
			}

			// Or if none of those is happening, we might like to do a horizontal zoom or scroll - only if [vertical
			// scroll isn't changed], and we're not on keyboard view
			else {
				if (getCurrentUI() != &keyboardScreen) {

					if (getCurrentUI() == &arrangerView) {
						if (currentSong->xZoom[NAVIGATION_ARRANGEMENT] != action->xZoomArranger[time]) {
							whichAnimation = Animation::ZOOM;
						}

						else if (currentSong->xScroll[NAVIGATION_ARRANGEMENT] != action->xScrollArranger[time]) {
							whichAnimation = Animation::SCROLL;
						}
					}

					else {
						if (currentSong->xZoom[NAVIGATION_CLIP] != action->xZoomClip[time]) {
							whichAnimation = Animation::ZOOM;
						}

						else if (currentSong->xScroll[NAVIGATION_CLIP] != action->xScrollClip[time]) {
							whichAnimation = Animation::SCROLL;
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
			int32_t totalNumClips =
			    currentSong->sessionClips.getNumElements() + currentSong->arrangementOnlyClips.getNumElements();
			if (action->numClipStates == totalNumClips) {

				// NOTE: i ranges over all clips, not just instrument clips
				int32_t i = 0;

				for (Clip* clip : AllClips::everywhere(currentSong)) {
					// clip->modKnobMode = action->clipStates[i].modKnobMode;
					if (clip->type == ClipType::INSTRUMENT) {
						InstrumentClip* instrumentClip = (InstrumentClip*)clip;
						instrumentClip->yScroll = action->clipStates[i].yScrollSessionView[time];
						instrumentClip->affectEntire = action->clipStates[i].affectEntire;
						instrumentClip->wrapEditing = action->clipStates[i].wrapEditing;
						instrumentClip->wrapEditLevel = action->clipStates[i].wrapEditLevel;

						if (clip->output->type == OutputType::KIT) {
							Kit* kit = (Kit*)clip->output;
							Drum* currentSelectedDrum = kit->selectedDrum;
							if (action->clipStates[i].selectedDrumIndex == -1) {
								kit->selectedDrum = NULL;
							}
							else {
								kit->selectedDrum = kit->getDrumFromIndex(action->clipStates[i].selectedDrumIndex);
							}
							// if affect entire is disabled and we've updated drum selection
							// need to update the mod controllable context that the gold knobs are editing
							if (!instrumentClip->affectEntire && currentSelectedDrum != kit->selectedDrum) {
								view.setActiveModControllableTimelineCounter(instrumentClip);
							}
						}
					}

					i++;
				}
			}
			else {
				D_PRINTLN("clip states wrong number so not restoring");
			}
		}

		// Vertical scroll
		currentSong->songViewYScroll = action->yScrollSongView[time];
		currentSong->arrangementYScroll = action->yScrollArranger[time];

		// Musical Scale
		currentSong->key.modeNotes = action->modeNotes[time];

		// Other stuff
		// currentSong->modKnobMode = action->modKnobModeSongView;
		currentSong->affectEntire = action->affectEntireSongView;
		currentSong->tripletsOn = action->tripletsOn;
		currentSong->tripletsLevel = action->tripletsLevel;

		// Now do the animation we decided on - for animations which we prefer to set up before reverting the actual
		// action
		if (whichAnimation == Animation::SCROLL && getCurrentUI() == &arrangerView) {
			bool worthDoingAnimation = arrangerView.initiateXScroll(action->xScrollArranger[time]);
			if (!worthDoingAnimation) {
				whichAnimation = Animation::NONE;
				goto otherOption;
			}
		}
		else {
otherOption:
			if (getCurrentUI() != &arrangerView || whichAnimation != Animation::ZOOM) {
				currentSong->xScroll[NAVIGATION_ARRANGEMENT] =
				    action->xScrollArranger[time]; // Have to do this if we didn't do the actual scroll animation yet
				                                   // some scrolling happened
			}
		}

		if (whichAnimation == Animation::SCROLL && getCurrentUI() != &arrangerView) {
			((TimelineView*)getCurrentUI())->initiateXScroll(action->xScrollClip[time]);
		}
		else if (getCurrentUI() == &arrangerView || whichAnimation != Animation::ZOOM) {
			currentSong->xScroll[NAVIGATION_CLIP] =
			    action->xScrollClip[time]; // Have to do this if we didn't do the actual scroll animation yet some
			                               // scrolling happened
		}

		if (whichAnimation == Animation::ZOOM) {
			if (getCurrentUI() == &arrangerView) {
				arrangerView.initiateXZoom(
				    howMuchMoreMagnitude(action->xZoomArranger[time], arrangerZoomBeforeTransition),
				    action->xScrollArranger[time], arrangerZoomBeforeTransition);
			}
			else {
				((TimelineView*)getCurrentUI())
				    ->initiateXZoom(howMuchMoreMagnitude(action->xZoomClip[time], songZoomBeforeTransition),
				                    action->xScrollClip[time], songZoomBeforeTransition);
			}
		}

		else if (whichAnimation == Animation::CLIP_MINDER_TO_SESSION) {
			sessionView.transitionToSessionView();
		}

		else if (whichAnimation == Animation::SESSION_TO_CLIP_MINDER) {
			sessionView.transitionToViewForClip(action->currentClip);
			goto currentClipSwitchedOver; // Skip the below - our call to transitionToViewForClip will switch it over
			                              // for us
		}

		// Swap currentClip over. Can only do this after calling transitionToSessionView(). Previously, we did this much
		// earlier, causing a crash. Hopefully moving it later here is ok...
		if (action->currentClip) { // If song just loaded and we hadn't been into ClipMinder yet, this would be NULL,
			                       // and we don't want to set currentSong->currentClip back to this
			currentSong->setCurrentClip(action->currentClip);
		}
	}

currentClipSwitchedOver:

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	Error error = action->revert(time, modelStack);

	// Some "animations", we prefer to do after we've reverted the action
	if (whichAnimation == Animation::ENTER_KEYBOARD_VIEW) {
		changeRootUI(&keyboardScreen);
	}

	else if (whichAnimation == Animation::EXIT_KEYBOARD_VIEW) {

		if (getCurrentClip()->onAutomationClipView) {
			changeRootUI(&automationView);
		}
		else {
			changeRootUI(&instrumentClipView);
		}
	}

	else if (whichAnimation == Animation::ENTER_AUTOMATION_VIEW) {
		changeRootUI(&automationView);
	}

	else if (whichAnimation == Animation::EXIT_AUTOMATION_VIEW) {
		automationView.resetShortcutBlinking();
		if (getCurrentClip()->type == ClipType::INSTRUMENT) {
			changeRootUI(&instrumentClipView);
		}
		else {
			changeRootUI(&audioClipView);
		}
	}

	else if (whichAnimation == Animation::CHANGE_CLIP) {
		if (action->view != getCurrentUI()) {
			changeRootUI(action->view);
		}
		else {
			getCurrentUI()->focusRegained();
			renderingNeededRegardlessOfUI(); // Didn't have this til March 2020, and stuff didn't update. Guess this is
			                                 // just needed? Can't remember specifics just now
		}
	}

	else if (whichAnimation == Animation::CLIP_MINDER_TO_ARRANGEMENT) {
		changeRootUI(&arrangerView);
	}

	else if (whichAnimation == Animation::ARRANGEMENT_TO_CLIP_MINDER) {
		if (getCurrentClip()->type == ClipType::AUDIO) {
			changeRootUI(&audioClipView);
		}
		else if (getCurrentInstrumentClip()->onKeyboardScreen) {
			changeRootUI(&keyboardScreen);
		}
		else if (getCurrentClip()->onAutomationClipView) {
			changeRootUI(&automationView);
		}
		else {
			changeRootUI(&instrumentClipView);
		}
	}

	else if (whichAnimation == Animation::SESSION_TO_ARRANGEMENT) {
		changeRootUI(&arrangerView);
	}

	else if (whichAnimation == Animation::ARRANGEMENT_TO_SESSION) {
		changeRootUI(&sessionView);
	}

	if (updateVisually) {
		UI* currentUI = getCurrentUI();

		if (currentUI == &instrumentClipView) {
			// If we're not animating away from this view (but something like scrolling sideways would be allowed)
			if (whichAnimation != Animation::CLIP_MINDER_TO_SESSION
			    && whichAnimation != Animation::CLIP_MINDER_TO_ARRANGEMENT) {
				instrumentClipView.recalculateColours();
				if (whichAnimation == Animation::NONE) {
					uiNeedsRendering(currentUI);
				}
			}
		}
		else if (currentUI == &automationView) {
			// If we're not animating away from this view (but something like scrolling sideways would be allowed)
			if (whichAnimation != Animation::CLIP_MINDER_TO_SESSION
			    && whichAnimation != Animation::CLIP_MINDER_TO_ARRANGEMENT) {
				if (getCurrentClip()->type == ClipType::INSTRUMENT) {
					instrumentClipView.recalculateColours();
				}
				if (whichAnimation == Animation::NONE) {
					uiNeedsRendering(currentUI);
				}
			}
		}
		else if (currentUI == &audioClipView) {
			if (whichAnimation == Animation::NONE) {
				uiNeedsRendering(currentUI);
			}
		}
		else if (currentUI == &keyboardScreen) {
			if (whichAnimation != Animation::ENTER_KEYBOARD_VIEW) {
				uiNeedsRendering(currentUI, 0xFFFFFFFF, 0);
			}
		}
		// Got to try this even if we're supposedly doing a horizontal scroll animation or something cos that may have
		// failed if the Clip wasn't long enough before we did the action->revert() ...
		else if (currentUI == &sessionView) {
			uiNeedsRendering(currentUI, 0xFFFFFFFF, 0xFFFFFFFF);
		}
		else if (currentUI == &arrangerView) {
			arrangerView.repopulateOutputsOnScreen(whichAnimation == Animation::NONE);
		}

		// Usually need to re-display the mod LEDs etc, but not if either of these animations is happening, which means
		// that it'll happen anyway when the animation finishes - and also, if we just deleted the Clip which was the
		// activeModControllableClip, well that'll temporarily be pointing to invalid stuff. Check the actual UI mode
		// rather than the whichAnimation variable we've been using in this function, because under some circumstances
		// that'll bypass the actual animation / UI-mode. We would also put the "explode" animation for transitioning
		// *to* arranger here, but it just doesn't get used during reversion.
		if (!isUIModeActive(UI_MODE_AUDIO_CLIP_COLLAPSING) && !isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)
		    && !isUIModeActive(UI_MODE_IMPLODE_ANIMATION)) {
			view.setKnobIndicatorLevels();
			view.setModLedStates();
		}

		// So long as we're not gonna animate to different UI...
		switch (whichAnimation) {
		case Animation::CLIP_MINDER_TO_SESSION:
		case Animation::SESSION_TO_CLIP_MINDER:
		case Animation::CLIP_MINDER_TO_ARRANGEMENT:
		case Animation::ARRANGEMENT_TO_CLIP_MINDER:
			break;

		default:
			ClipMinder* clipMinder = getCurrentUI()->toClipMinder();
			if (clipMinder) {
				if (getCurrentClip()->type == ClipType::INSTRUMENT) {
					((InstrumentClipMinder*)clipMinder)->setLedStates();
				}
			}
			else if (getCurrentUI() == &sessionView) {
				sessionView.setLedStates();
			}
			if (auto* timelineView = getCurrentUI()->toTimelineView()) {
				timelineView->setTripletsLEDState();
			}
		}
	}

	if (playbackHandler.isEitherClockActive()) {
		currentPlaybackMode->reversionDone(); // Re-gets automation and stuff
	}

	// If there was an actual error in the reversion itself...
	if (error != Error::NONE) {
		display->displayError(error);

		deleteAllLogs();
	}
}

void ActionLogger::closeAction(ActionType actionType) {
	if (firstAction[BEFORE] && firstAction[BEFORE]->type == actionType) {
		firstAction[BEFORE]->openForAdditions = false;
	}
}

void ActionLogger::closeActionUnlessCreatedJustNow(ActionType actionType) {
	if (firstAction[BEFORE] && firstAction[BEFORE]->type == actionType
	    && firstAction[BEFORE]->creationTime != AudioEngine::audioSampleTimer) {
		firstAction[BEFORE]->openForAdditions = false;
	}
}

void ActionLogger::deleteAllLogs() {
	deleteLog(BEFORE);
	deleteLog(AFTER);
}

void ActionLogger::deleteLog(int32_t time) {
	while (firstAction[time]) {
		Action* toDelete = firstAction[time];

		firstAction[time] = firstAction[time]->nextAction;

		toDelete->prepareForDestruction(time, currentSong);
		toDelete->~Action();
		delugeDealloc(toDelete);
	}
}

// You must not call this during the card routine - though I've lost track of the exact reason why not - is it just
// because we could then be in the middle of executing whichever function accessed the card and we don't know if things
// will break?
void ActionLogger::undo() {

	// Before we go and revert the most recent Action, there are a few recording-related states we first want to have a
	// go at cancelling out of. These are treated as special cases here rather than being Consequences because they're
	// never redoable: their "undoing" is a special case of cancellation.

	// But, this is to be done very sparingly! I used to have more of these which did things like deleting Clips for
	// which linear recording was ongoing. But then what if other Consequences, e.g. param automation, had been recorded
	// for those? Reverting those would call functions on invalid pointers. So instead, do just use regular Actions and
	// Consequences for everything possible. And definitely don't delete any Clips here.

	// If currently recording an arrangement from session, we have to stop doing so first
	if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
		playbackHandler.recording = RecordingMode::OFF;
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
	else if (playbackHandler.playbackState && playbackHandler.recording != RecordingMode::OFF
	         && currentPlaybackMode == &arrangement) {
		arrangement.endAnyLinearRecording();
	}

	// Ok, do the actual undo.
	if (revert(BEFORE)) {
displayUndoMessage:
#ifdef undoLedX
		indicator_leds::indicateAlertOnLed(undoLedX, undoLedY);
#else
		display->consoleText("Undo");
#endif
	}
}

// You must not call this during the card routine - though I've lost track of the exact reason why not - is it just
// because we could then be in the middle of executing whichever function accessed the card and we don't know if things
// will break?
void ActionLogger::redo() {
	if (revert(AFTER)) {
#ifdef redoLedX
		indicator_leds::indicateAlertOnLed(redoLedX, redoLedY);
#else
		display->consoleText("Redo");
#endif
	}
}

const uint32_t reversionUIModes[] = {
    UI_MODE_AUDITIONING,
    UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION,
    UI_MODE_CLIP_PRESSED_IN_SONG_VIEW,
    UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON,
    0,
};

bool ActionLogger::allowedToDoReversion() {
	return (currentSong && getCurrentUI() == getRootUI() && isUIModeWithinRange(reversionUIModes));
}

void ActionLogger::notifyClipRecordingAborted(Clip* clip) {

	// If there's an Action which only recorded the beginning of this Clip recording, we don't want it anymore.
	if (firstAction[BEFORE] && firstAction[BEFORE]->type == ActionType::RECORD) {
		Consequence* firstConsequence = firstAction[BEFORE]->firstConsequence;
		if (!firstConsequence->next && firstConsequence->type == Consequence::CLIP_BEGIN_LINEAR_RECORD) {
			if (clip == ((ConsequenceClipBeginLinearRecord*)firstConsequence)->clip) {
				deleteLastAction();
			}
		}
	}
}

// This function relies on Consequences having been sequentially added for each subsequent "mini action", so looking at
// the noteRowId of the most recent one, we can then know that all further Consequences until we see the same noteRowId
// again are part of the same "mini action". This will get called in some cases (Action types) were only one NoteRow,
// not many, could have had the editing done to it: that's fine too, and barely any time is really wasted here. Returns
// whether whole Action was reverted, which is the only case where visual updating / rendering, and also the calling of
// expectEvent(), would have taken place
bool ActionLogger::undoJustOneConsequencePerNoteRow(ModelStack* modelStack) {

	bool revertedWholeAction = false;

	Consequence* firstConsequence = firstAction[BEFORE]->firstConsequence;
	if (firstConsequence) { // Should always be true

		// Work out if multiple Consequences per NoteRow (see big comment above)
		int32_t firstNoteRowId = ((ConsequenceNoteArrayChange*)firstConsequence)->noteRowId;

		Consequence* thisConsequence = firstConsequence->next;
		while (thisConsequence) {
			if (thisConsequence->type == Consequence::NOTE_ARRAY_CHANGE
			    && ((ConsequenceNoteArrayChange*)thisConsequence)->noteRowId == firstNoteRowId) {
				goto gotMultipleConsequencesPerNoteRow;
			}

			thisConsequence = thisConsequence->next;
		}

		// If multiple Consequences per NoteRow, just revert most recent one per NoteRow
		if (false) {
gotMultipleConsequencesPerNoteRow:
			do {
				firstConsequence->revert(BEFORE,
				                         modelStack); // Unlike reverting a whole Action, this doesn't update anything
				                                      // visually or call any expectEvent() functions
				firstAction[BEFORE]->firstConsequence = firstConsequence->next;

				firstConsequence->prepareForDestruction(BEFORE, modelStack->song);
				firstConsequence->~Consequence();
				delugeDealloc(firstConsequence);
				firstConsequence = firstAction[BEFORE]->firstConsequence;
			} while (thisConsequence->type != Consequence::NOTE_ARRAY_CHANGE
			         || ((ConsequenceNoteArrayChange*)firstConsequence)->noteRowId != firstNoteRowId);

			D_PRINTLN("did secret undo, just one Consequence");
		}

		// Or if only one Consequence (per NoteRow), revert whole Action
		else {
			revert(BEFORE, true, false);
			D_PRINTLN("did secret undo, whole Action");
			revertedWholeAction = true;
		}

		deleteLog(AFTER);
	}

	return revertedWholeAction;
}
