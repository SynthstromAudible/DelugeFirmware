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

#include <ArrangerView.h>
#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <ClipInstance.h>
#include <DString.h>
#include <InstrumentClip.h>
#include <InstrumentClipMinder.h>
#include <InstrumentClipView.h>
#include <ParamManager.h>
#include <SessionView.h>
#include "functions.h"
#include "numericdriver.h"
#include "uart.h"
#include "View.h"
#include "NoteRow.h"
#include "UI.h"
#include "ActionLogger.h"
#include "GeneralMemoryAllocator.h"
#include "Session.h"
#include "instrument.h"
#include "Arrangement.h"
#include <new>
#include "storagemanager.h"
#include "LoadInstrumentPresetUI.h"
#include "AudioClip.h"
#include "AudioOutput.h"
#include "AudioInputSelector.h"
#include "song.h"
#include "KeyboardScreen.h"
#include "AudioClipView.h"
#include "WaveformRenderer.h"
#include "SampleRecorder.h"
#include "MelodicInstrument.h"
#include "MenuItemColour.h"
#include "PadLEDs.h"
#include "IndicatorLEDs.h"
#include "Buttons.h"
#include "extern.h"
#include "Sample.h"
#include "playbackhandler.h"
#include "loadsongui.h"
#include "uitimermanager.h"
#include "FileItem.h"
#include "soundeditor.h"

#if HAVE_OLED
#include "oled.h"
#endif

extern "C" {
#include "cfunctions.h"
#include "sio_char.h"
}

SessionView sessionView;

extern int8_t defaultAudioClipOverdubOutputCloning;

SessionView::SessionView() {
	xScrollBeforeFollowingAutoExtendingLinearRecording = -1;
}

bool SessionView::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING) {
		*cols = 0xFFFFFFFD;
		*rows = 0;
		for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
			Clip* clip = getClipOnScreen(yDisplay);
			if (clip && !clip->armedForRecording) {
				*rows |= (1 << yDisplay);
			}
		}
		return true;
	}
	else if (playbackHandler.playbackState && currentPlaybackMode == &arrangement) {
		*cols = 0b11;
		return true;
	}
	else return false;
}

bool SessionView::opened() {

	if (playbackHandler.playbackState && currentPlaybackMode == &arrangement) PadLEDs::skipGreyoutFade();

	IndicatorLEDs::setLedState(crossScreenEditLedX, crossScreenEditLedY, false);
	IndicatorLEDs::setLedState(scaleModeLedX, scaleModeLedY, false);

	focusRegained();

	return true;
}

void SessionView::focusRegained() {

	bool doingRender = (currentUIMode != UI_MODE_ANIMATION_FADE);
	redrawClipsOnScreen(doingRender); // We want this here, not just in opened(), because after coming back from
	                                  // loadInstrumentPresetUI, need to at least redraw, and also really need to
	                                  // re-render stuff in case note-tails-being-allowed has changed

	ClipNavigationTimelineView::focusRegained();
	view.focusRegained();
	view.setActiveModControllableTimelineCounter(currentSong);

	selectedClipYDisplay = 255;
#if HAVE_OLED
	setCentralLEDStates();
#else
	redrawNumericDisplay();
#endif
	IndicatorLEDs::setLedState(backLedX, backLedY, false);

	setLedStates();

	currentSong->lastClipInstanceEnteredStartPos = -1;
}

int SessionView::buttonAction(int x, int y, bool on, bool inCardRoutine) {

	int newInstrumentType;

	// Clip-view button
	if (x == clipViewButtonX && y == clipViewButtonY) {
		if (on && currentUIMode == UI_MODE_NONE && playbackHandler.recording != RECORDING_ARRANGEMENT) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			transitionToViewForClip(); // May fail if no currentClip
		}
	}

	// Song-view button without shift

	// Arranger view button, or if there isn't one then song view button
#ifdef arrangerViewButtonX
	else if (x == arrangerViewButtonX && y == arrangerViewButtonY) {
#else
	else if (x == sessionViewButtonX && y == sessionViewButtonY && !Buttons::isShiftButtonPressed()) {
#endif
		if (on) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			// If holding record button...
			if (Buttons::isButtonPressed(recordButtonX, recordButtonY)) {
				Buttons::recordButtonPressUsedUp = true;

				// Make sure we weren't already playing...
				if (!playbackHandler.playbackState) {

					Action* action = actionLogger.getNewAction(ACTION_ARRANGEMENT_RECORD, false);

					arrangerView.xScrollWhenPlaybackStarted = currentSong->xScroll[NAVIGATION_ARRANGEMENT];
					if (action) action->posToClearArrangementFrom = arrangerView.xScrollWhenPlaybackStarted;

					currentSong->clearArrangementBeyondPos(
					    arrangerView.xScrollWhenPlaybackStarted,
					    action); // Want to do this before setting up playback or place new instances
					int error = currentSong->placeFirstInstancesOfActiveClips(arrangerView.xScrollWhenPlaybackStarted);

					if (error) {
						numericDriver.displayError(error);
						return ACTION_RESULT_DEALT_WITH;
					}
					playbackHandler.recording = RECORDING_ARRANGEMENT;
					playbackHandler.setupPlaybackUsingInternalClock();

					arrangement.playbackStartedAtPos =
					    arrangerView.xScrollWhenPlaybackStarted; // Have to do this after setting up playback

					IndicatorLEDs::blinkLed(recordLedX, recordLedY, 255, 1);
					IndicatorLEDs::blinkLed(sessionViewLedX, sessionViewLedY, 255, 1);
				}
			}
			else if (currentUIMode == UI_MODE_NONE) {
				if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
					currentSong->endInstancesOfActiveClips(playbackHandler.getActualArrangementRecordPos());
					currentSong
					    ->resumeClipsClonedForArrangementRecording(); // Must call before calling getArrangementRecordPos(), cos that detaches the cloned Clip
					playbackHandler.recording = RECORDING_OFF;
					view.setModLedStates();
					playbackHandler.setLedStates();
				}
				else {
					goToArrangementEditor();
				}
			}

			else if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
				if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
					numericDriver.displayPopup(HAVE_OLED ? "Recording to arrangement" : "CANT");
					return ACTION_RESULT_DEALT_WITH;
				}

				actionLogger.deleteAllLogs();

				Clip* clip = getClipOnScreen(selectedClipYDisplay);
				Output* output = clip->output;
				int instrumentIndex = currentSong->getOutputIndex(output);
				currentSong->arrangementYScroll = instrumentIndex - selectedClipPressYDisplay;

				int32_t posPressed = arrangerView.getPosFromSquare(selectedClipPressXDisplay);
				int32_t proposedStartPos = posPressed;

				int i = output->clipInstances.search(proposedStartPos, LESS);
				ClipInstance* otherInstance = output->clipInstances.getElement(i);
				if (otherInstance) {
					if (otherInstance->pos + otherInstance->length > proposedStartPos) {
moveAfterClipInstance:
						proposedStartPos = ((otherInstance->pos + otherInstance->length - 1)
						                        / currentSong->xZoom[NAVIGATION_ARRANGEMENT]
						                    + 1)
						                   * currentSong->xZoom[NAVIGATION_ARRANGEMENT];
					}
				}

				// Look at the next ClipInstance
				i++;
				otherInstance = output->clipInstances.getElement(i);
				if (otherInstance) {
					if (otherInstance->pos < proposedStartPos + clip->loopLength) {
						goto moveAfterClipInstance;
					}
				}

				// Make sure it won't be extending beyond numerical limit
				if (proposedStartPos > MAX_SEQUENCE_LENGTH - clip->loopLength) {
					numericDriver.displayPopup(HAVE_OLED ? "Clip would breach max arrangement length" : "CANT");
					return ACTION_RESULT_DEALT_WITH;
				}

				// If we're here, we're ok!
				int error = output->clipInstances.insertAtIndex(i);
				if (error) {
					numericDriver.displayError(error);
					return ACTION_RESULT_DEALT_WITH;
				}

				ClipInstance* newInstance = output->clipInstances.getElement(i);
				newInstance->pos = proposedStartPos;
				newInstance->clip = clip;
				newInstance->length = clip->loopLength;
				arrangement.rowEdited(output, proposedStartPos, proposedStartPos + clip->loopLength, NULL, newInstance);

				int32_t howMuchLater = proposedStartPos - posPressed;

				arrangerView.xPressed = selectedClipPressXDisplay;
				arrangerView.yPressedEffective = selectedClipPressYDisplay;
				arrangerView.yPressedActual = selectedClipPressYDisplay;
				arrangerView.actionOnDepress = false;
				arrangerView.desiredLength = clip->loopLength;
				arrangerView.originallyPressedClipActualLength = clip->loopLength;
				arrangerView.pressedClipInstanceIndex = i;
				arrangerView.pressedClipInstanceXScrollWhenLastInValidPosition =
				    currentSong->xScroll[NAVIGATION_ARRANGEMENT] + howMuchLater;
				arrangerView.pressedClipInstanceOutput = clip->output;
				arrangerView.pressedClipInstanceIsInValidPosition = true;

				currentUIMode = UI_MODE_HOLDING_ARRANGEMENT_ROW;

				arrangerView.repopulateOutputsOnScreen(false);
				arrangerView.putDraggedClipInstanceInNewPosition(output);
				goToArrangementEditor();
			}
		}
	}

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	// Affect-entire button
	else if (x == affectEntireButtonX && y == affectEntireButtonY) {
		if (on && currentUIMode == UI_MODE_NONE) {
			currentSong->affectEntire = !currentSong->affectEntire;
			view.setActiveModControllableTimelineCounter(currentSong);
		}
	}
#endif

	// Record button - adds to what MatrixDriver does with it
	else if (x == recordButtonX && y == recordButtonY) {
		if (on) {
			if (isNoUIModeActive()) {
				uiTimerManager.setTimer(TIMER_UI_SPECIFIC, 500);
				view.blinkOn = true;
			}
			else goto notDealtWith;
		}
		else {
			if (isUIModeActive(UI_MODE_VIEWING_RECORD_ARMING)) {
				exitUIMode(UI_MODE_VIEWING_RECORD_ARMING);
				PadLEDs::reassessGreyout(false);
				uiNeedsRendering(this, 0, 0xFFFFFFFF);
			}
			else goto notDealtWith;
		}
		return ACTION_RESULT_NOT_DEALT_WITH; // Make the MatrixDriver do its normal thing with it too
	}

	// If save / delete button pressed, delete the Clip!
	else if (x == saveButtonX && y == saveButtonY && currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
		if (on) {

			if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
				numericDriver.displayPopup(HAVE_OLED ? "Recording to arrangement" : "CANT");
				performActionOnPadRelease = false;
				return ACTION_RESULT_DEALT_WITH;
			}

			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			actionLogger.deleteAllLogs();
			int yDisplay = selectedClipYDisplay;
			clipPressEnded();
			removeClip(yDisplay);
		}
	}

	// Select encoder button
	else if (x == selectEncButtonX && y == selectEncButtonY && !Buttons::isShiftButtonPressed()) {
		if (on) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			if (currentUIMode == UI_MODE_HOLDING_SECTION_PAD) {
				if (performActionOnSectionPadRelease) beginEditingSectionRepeatsNum();
				else {
					currentSong->sections[sectionPressed].numRepetitions = 0;
					drawSectionRepeatNumber();
				}
			}
			else if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
				actionLogger.deleteAllLogs();
				performActionOnPadRelease = false;
				replaceInstrumentClipWithAudioClip();
			}
			else if (currentUIMode == UI_MODE_NONE) {
				if (session.hasPlaybackActive() && session.launchEventAtSwungTickCount) {
					session.cancelAllArming();
					session.cancelAllLaunchScheduling();
					session.lastSectionArmed = 255;
#if HAVE_OLED
					renderUIsForOled();
#else
					redrawNumericDisplay();
#endif
					uiNeedsRendering(this, 0, 0xFFFFFFFF);
				}
				else {
					numericDriver.setNextTransitionDirection(1);
					soundEditor.setup(NULL, &rootMenuSong, 0);
					openUI(&soundEditor);
				}
			}
		}
	}

	// Which-instrument-type buttons
	else if (x == synthButtonX && y == synthButtonY) {
		newInstrumentType = INSTRUMENT_TYPE_SYNTH;

changeInstrumentType:
		if (on && currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW && !Buttons::isShiftButtonPressed()) {

			performActionOnPadRelease = false;

			if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
				numericDriver.displayPopup(HAVE_OLED ? "Recording to arrangement" : "CANT");
				return ACTION_RESULT_DEALT_WITH;
			}

			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

			Clip* clip = getClipOnScreen(selectedClipYDisplay);

			// If AudioClip, we have to convert back to an InstrumentClip
			if (clip->type == CLIP_TYPE_AUDIO) {
				actionLogger.deleteAllLogs();
				replaceAudioClipWithInstrumentClip(newInstrumentType);
			}

			// Or if already an InstrumentClip, changing Instrument type is easier
			else {

				InstrumentClip* instrumentClip = (InstrumentClip*)clip;
				// If load button held, go into LoadInstrumentPresetUI
				if (Buttons::isButtonPressed(loadButtonX, loadButtonY)) {

					// Can't do that for MIDI or CV Clips though
					if (newInstrumentType == INSTRUMENT_TYPE_MIDI_OUT || newInstrumentType == INSTRUMENT_TYPE_CV) {
						goto doActualSimpleChange;
					}

					Instrument* instrument = (Instrument*)instrumentClip->output;

					actionLogger.deleteAllLogs();

					currentUIMode = UI_MODE_NONE;
					selectedClipYDisplay = 255;

					Browser::instrumentTypeToLoad = newInstrumentType;
					loadInstrumentPresetUI.instrumentToReplace = instrument;
					loadInstrumentPresetUI.instrumentClipToLoadFor = instrumentClip;
					openUI(&loadInstrumentPresetUI);
				}

				// Otherwise, just change the instrument type
				else {
doActualSimpleChange:
					char modelStackMemory[MODEL_STACK_MAX_SIZE];
					ModelStackWithTimelineCounter* modelStack =
					    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, instrumentClip);

					view.changeInstrumentType(newInstrumentType, modelStack, true);
				}
			}

			uiNeedsRendering(this, 1 << selectedClipYDisplay, 0);
		}
	}
	else if (x == kitButtonX && y == kitButtonY) {
		newInstrumentType = INSTRUMENT_TYPE_KIT;
		goto changeInstrumentType;
	}
	else if (x == midiButtonX && y == midiButtonY) {
		newInstrumentType = INSTRUMENT_TYPE_MIDI_OUT;
		goto changeInstrumentType;
	}
	else if (x == cvButtonX && y == cvButtonY) {
		newInstrumentType = INSTRUMENT_TYPE_CV;
		goto changeInstrumentType;
	}

	else {
notDealtWith:
		return TimelineView::buttonAction(x, y, on, inCardRoutine);
	}

	return ACTION_RESULT_DEALT_WITH;
}

void SessionView::goToArrangementEditor() {
	currentSong->xZoomForReturnToSongView = currentSong->xZoom[NAVIGATION_CLIP];
	currentSong->xScrollForReturnToSongView = currentSong->xScroll[NAVIGATION_CLIP];
	changeRootUI(&arrangerView);
}

void SessionView::beginEditingSectionRepeatsNum() {
	performActionOnSectionPadRelease = false;
	drawSectionRepeatNumber();
	uiTimerManager.unsetTimer(TIMER_UI_SPECIFIC);
}

int SessionView::padAction(int xDisplay, int yDisplay, int on) {

	Clip* clip = getClipOnScreen(yDisplay);
	int clipIndex = yDisplay + currentSong->songViewYScroll;

	// If we tapped on a Clip's main pads...
	if (xDisplay < displayWidth) {

		// Press down
		if (on) {

			Buttons::recordButtonPressUsedUp = true;

			if (!Buttons::isShiftButtonPressed()) {

				if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING) goto holdingRecord;

				// If no Clip previously pressed...
				if (currentUIMode == UI_MODE_NONE) {

					// If they're holding down the record button...
					if (Buttons::isButtonPressed(recordButtonX, recordButtonY)) {

holdingRecord:
						// If doing recording stuff, create a "pending overdub".
						// We may or may not be doing a tempoless record and need to finish that up.
						if (playbackHandler.playbackState && currentPlaybackMode == &session) {

							Clip* sourceClip = getClipOnScreen(yDisplay + 1);

							if (!sourceClip) return ACTION_RESULT_DEALT_WITH;

							// If already has a pending overdub, get out
							if (currentSong->getPendingOverdubWithOutput(sourceClip->output))
								return ACTION_RESULT_DEALT_WITH;

							if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
								numericDriver.displayPopup(HAVE_OLED ? "Recording to arrangement" : "CANT");
								return ACTION_RESULT_DEALT_WITH;
							}

							if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

							int clipIndex = yDisplay + currentSong->songViewYScroll + 1;

							// If source clip currently recording, arm it to stop (but not if tempoless recording)
							if (playbackHandler.isEitherClockActive() && sourceClip->getCurrentlyRecordingLinearly()
							    && !sourceClip->armState) {
								session.toggleClipStatus(sourceClip, &clipIndex, false, INTERNAL_BUTTON_PRESS_LATENCY);
							}

							int newOverdubNature =
							    (xDisplay < displayWidth) ? OVERDUB_NORMAL : OVERDUB_CONTINUOUS_LAYERING;
							Clip* overdub =
							    currentSong->createPendingNextOverdubBelowClip(sourceClip, clipIndex, newOverdubNature);
							if (overdub) {

								session.scheduleOverdubToStartRecording(overdub, sourceClip);

								if (!playbackHandler.recording) {
									playbackHandler.recording = RECORDING_NORMAL;
									playbackHandler.setLedStates();
								}

								// Since that was all effective, let's exit out of UI_MODE_VIEWING_RECORD_ARMING too
								if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING) {
									uiTimerManager.unsetTimer(TIMER_UI_SPECIFIC);
									currentUIMode = UI_MODE_NONE;
									PadLEDs::reassessGreyout(false);
									uiNeedsRendering(this, 0, 0xFFFFFFFF);
								}

								// If we were doing a tempoless record, now's the time to stop that and restart playback
								if (!playbackHandler.isEitherClockActive()) {
									playbackHandler.finishTempolessRecording(true, INTERNAL_BUTTON_PRESS_LATENCY,
									                                         false);
								}
							}
							else if (currentSong->anyClipsSoloing) {
								numericDriver.displayPopup(HAVE_OLED ? "Can't create overdub while clips soloing"
								                                     : "SOLO");
							}
						}
					}

					// If Clip present here...
					else if (clip) {

						// If holding down tempo knob...
						if (Buttons::isButtonPressed(tempoEncButtonX, tempoEncButtonY)) {
							playbackHandler.grabTempoFromClip(clip);
						}

						// If it's a pending overdub, delete it
						else if (clip->isPendingOverdub) {
removePendingOverdub:
							if (sdRoutineLock)
								return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // Possibly not quite necessary...
							removeClip(yDisplay);
							session.justAbortedSomeLinearRecording();
						}

						// Or, normal action - select the pressed Clip
						else {

							selectedClipYDisplay = yDisplay;
startHoldingDown:
							selectedClipPressYDisplay = yDisplay;
							currentUIMode = UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
							selectedClipPressXDisplay = xDisplay;
							performActionOnPadRelease = true;
							selectedClipTimePressed = AudioEngine::audioSampleTimer;
							view.setActiveModControllableTimelineCounter(clip);
							view.displayOutputName(clip->output, true, clip);
#if HAVE_OLED
							OLED::sendMainImage();
#endif
						}
					}

					// Otherwise, try and create one
					else {

						if (Buttons::isButtonPressed(recordButtonX, recordButtonY)) return ACTION_RESULT_DEALT_WITH;
						if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

						//if (possiblyCreatePendingNextOverdub(clipIndex, OVERDUB_EXTENDING)) return ACTION_RESULT_DEALT_WITH;

						clip = createNewInstrumentClip(yDisplay);
						if (!clip) return ACTION_RESULT_DEALT_WITH;

						int numClips = currentSong->sessionClips.getNumElements();
						if (clipIndex < 0) clipIndex = 0;
						else if (clipIndex >= numClips) clipIndex = numClips - 1;

						selectedClipYDisplay = clipIndex - currentSong->songViewYScroll;
						uiNeedsRendering(this, 0, 1 << selectedClipYDisplay);
						goto startHoldingDown;
					}
				}

				// If Clip previously already pressed, clone it to newly-pressed row
				else if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
					if (selectedClipYDisplay != yDisplay && performActionOnPadRelease) {

						if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
							numericDriver.displayPopup(HAVE_OLED ? "Recording to arrangement" : "CANT");
							return ACTION_RESULT_DEALT_WITH;
						}

						if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

						actionLogger.deleteAllLogs();
						cloneClip(selectedClipYDisplay, yDisplay);
						goto justEndClipPress;
					}
				}

				else if (currentUIMode == UI_MODE_MIDI_LEARN) {
					if (clip) {

						// AudioClip
						if (clip->type == CLIP_TYPE_AUDIO) {
							if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
							view.endMIDILearn();
							audioInputSelector.audioOutput = (AudioOutput*)clip->output;
							audioInputSelector.setupAndCheckAvailability();
							openUI(&audioInputSelector);
						}

						// InstrumentClip
						else {
midiLearnMelodicInstrumentAction:
							if (clip->output->type == INSTRUMENT_TYPE_SYNTH
							    || clip->output->type == INSTRUMENT_TYPE_MIDI_OUT
							    || clip->output->type == INSTRUMENT_TYPE_CV) {

								if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
								view.melodicInstrumentMidiLearnPadPressed(on, (MelodicInstrument*)clip->output);
							}
						}
					}
				}
			}
		}

		// Release
		else {

			// If Clip was pressed before...
			if (isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)) {

				// Stop stuttering if we are
				if (isUIModeActive(UI_MODE_STUTTERING)) {
					((ModControllableAudio*)view.activeModControllableModelStack.modControllable)
					    ->endStutter((ParamManagerForTimeline*)view.activeModControllableModelStack.paramManager);
				}

				if (performActionOnPadRelease && xDisplay == selectedClipPressXDisplay
				    && AudioEngine::audioSampleTimer - selectedClipTimePressed < (44100 >> 1)) {

					// Not allowed if recording arrangement
					if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
						numericDriver.displayPopup(HAVE_OLED ? "Recording to arrangement" : "CANT");
						goto justEndClipPress;
					}

					if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

					// Enter Clip
					Clip* clip = getClipOnScreen(selectedClipYDisplay);
					transitionToViewForClip(clip);
				}

				// If doing nothing, at least exit the submode - if this was that initial press
				else {
					if (yDisplay == selectedClipPressYDisplay && xDisplay == selectedClipPressXDisplay) {
justEndClipPress:
						if (sdRoutineLock)
							return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // If in card routine, might mean it's still loading an Instrument they selected,
							    // and we don't want the loading animation or anything to get stuck onscreen
						clipPressEnded();
					}
				}
			}

			else if (isUIModeActive(UI_MODE_MIDI_LEARN)) {
				if (clip && clip->type == CLIP_TYPE_INSTRUMENT) {
					uiNeedsRendering(this, 1 << yDisplay, 0);
					goto midiLearnMelodicInstrumentAction;
				}
			}

			// In all other cases, then if also inside card routine, do get it to remind us after. Especially important because it could be that
			// the user has actually pressed down on a pad, that's caused a new clip to be created and preset to load, which is still loading right now,
			// but the uiMode hasn't been set to "holding down" yet and control hasn't been released back to the user, and this is the user releasing their press,
			// so we definitely want to be reminded of this later after the above has happened.
			else {
				if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
		}
	}

	// Or, status or section (aka audition) pads
	else {

		if (playbackHandler.playbackState && currentPlaybackMode == &arrangement) {
			if (currentUIMode == UI_MODE_NONE) {
				if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
				playbackHandler.switchToSession();
			}
		}

		else {

			if (clip && clip->isPendingOverdub) {
				if (on && !currentUIMode) goto removePendingOverdub;
			}

			// Status pad
			if (xDisplay == displayWidth) {

				// If Clip is present here
				if (clip) {

					return view.clipStatusPadAction(clip, on, yDisplay);
				}
			}

			// Section pad
			else if (xDisplay == displayWidth + 1) {

				if (on && Buttons::isButtonPressed(recordButtonX, recordButtonY)
				    && (!currentUIMode || currentUIMode == UI_MODE_VIEWING_RECORD_ARMING)) {
					Buttons::recordButtonPressUsedUp = true;
					goto holdingRecord;
				}

				// If Clip is present here
				if (clip) {

					switch (currentUIMode) {
					case UI_MODE_MIDI_LEARN:
						if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
						view.sectionMidiLearnPadPressed(on, clip->section);
						break;

					case UI_MODE_NONE:
					case UI_MODE_CLIP_PRESSED_IN_SONG_VIEW:
					case UI_MODE_STUTTERING:
						performActionOnPadRelease = false;
						// No break
					case UI_MODE_HOLDING_SECTION_PAD:
						sectionPadAction(yDisplay, on);
						break;
					}
				}
			}
		}
	}

	return ACTION_RESULT_DEALT_WITH;
}

void SessionView::clipPressEnded() {
	currentUIMode = UI_MODE_NONE;
	view.setActiveModControllableTimelineCounter(currentSong);
#if HAVE_OLED
	renderUIsForOled();
	setCentralLEDStates();
#else
	redrawNumericDisplay();
#endif
	selectedClipYDisplay = 255;
}

void SessionView::sectionPadAction(uint8_t y, bool on) {

	Clip* clip = getClipOnScreen(y);

	if (!clip) return;

	if (on) {

		if (isNoUIModeActive()) {
			// If user wanting to change Clip's section
			if (Buttons::isShiftButtonPressed()) {

				// Not allowed if recording arrangement
				if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
					numericDriver.displayPopup(HAVE_OLED ? "Recording to arrangement" : "CANT");
					return;
				}

				actionLogger.deleteAllLogs();

				uint8_t oldSection = clip->section;

				clip->section = 255;

				bool sectionUsed[MAX_NUM_SECTIONS];
				memset(sectionUsed, 0, sizeof(sectionUsed));

				for (int c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
					Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);

					if (thisClip->section < MAX_NUM_SECTIONS) sectionUsed[thisClip->section] = true;
				}

				// Mark first unused section as available
				for (int i = 0; i < MAX_NUM_SECTIONS; i++) {
					if (!sectionUsed[i]) {
						sectionUsed[i] = true;
						break;
					}
				}

				do {
					oldSection = (oldSection + 1) % MAX_NUM_SECTIONS;
				} while (!sectionUsed[oldSection]);

				clip->section = oldSection;

				uiNeedsRendering(this, 0, 1 << y);
			}

			else {
				enterUIMode(UI_MODE_HOLDING_SECTION_PAD);
				performActionOnSectionPadRelease = true;
				sectionPressed = clip->section;
				uiTimerManager.setTimer(TIMER_UI_SPECIFIC, 300);
			}
		}
	}

	// Or, triggering actual section play, with de-press
	else {

		if (isUIModeActive(UI_MODE_HOLDING_SECTION_PAD)) {
			if (!Buttons::isShiftButtonPressed() && performActionOnSectionPadRelease) {
				session.armSection(sectionPressed, INTERNAL_BUTTON_PRESS_LATENCY);
			}
			exitUIMode(UI_MODE_HOLDING_SECTION_PAD);
#if HAVE_OLED
			OLED::removePopup();
#else
			redrawNumericDisplay();
#endif
			uiTimerManager.unsetTimer(TIMER_UI_SPECIFIC);
		}

		else if (isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)) {
			session.armSection(clip->section, INTERNAL_BUTTON_PRESS_LATENCY);
		}
	}
}

int SessionView::timerCallback() {
	switch (currentUIMode) {

	case UI_MODE_HOLDING_SECTION_PAD:
		beginEditingSectionRepeatsNum();
		break;

	case UI_MODE_NONE:
		if (Buttons::isButtonPressed(recordButtonX, recordButtonY)) {
			enterUIMode(UI_MODE_VIEWING_RECORD_ARMING);
			PadLEDs::reassessGreyout(false);
		case UI_MODE_VIEWING_RECORD_ARMING:
			uiNeedsRendering(this, 0, 0xFFFFFFFF);
			view.blinkOn = !view.blinkOn;
			uiTimerManager.setTimer(TIMER_UI_SPECIFIC, fastFlashTime);
		}
		break;
	}

	return ACTION_RESULT_DEALT_WITH;
}

void SessionView::drawSectionRepeatNumber() {
	int number = currentSong->sections[sectionPressed].numRepetitions;
	char const* outputText;
#if HAVE_OLED
	char buffer[21];
	if (number == -1) {
		outputText = "Launch non-\nexclusively"; // Need line break cos line splitter doesn't deal with hyphens.
	}
	else {
		outputText = buffer;
		strcpy(buffer, "Repeats: ");
		if (number == 0) {
			strcpy(&buffer[9], "infinite");
		}
		else {
			intToString(number, &buffer[9]);
		}
	}
	OLED::popupText(outputText, true);
#else
	char buffer[5];
	if (number == -1) {
		outputText = "SHAR";
	}
	else if (number == 0) {
		outputText = "INFI";
	}
	else {
		intToString(number, buffer);
		outputText = buffer;
	}
	numericDriver.setText(outputText, true, 255, true);
#endif
}

void SessionView::selectEncoderAction(int8_t offset) {
	if (currentUIMode == UI_MODE_HOLDING_SECTION_PAD) {
		if (performActionOnSectionPadRelease) beginEditingSectionRepeatsNum();
		else {
			int16_t* numRepetitions = &currentSong->sections[sectionPressed].numRepetitions;
			*numRepetitions += offset;
			if (*numRepetitions > 9999) *numRepetitions = 9999;
			else if (*numRepetitions < -1) *numRepetitions = -1;
			drawSectionRepeatNumber();
		}
	}
	else if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
		performActionOnPadRelease = false;

		if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
			numericDriver.displayPopup(HAVE_OLED ? "Recording to arrangement" : "CANT");
			return;
		}

		Clip* clip = getClipOnScreen(selectedClipYDisplay);

		if (clip->type == CLIP_TYPE_INSTRUMENT) {

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack =
			    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);

			view.navigateThroughPresetsForInstrumentClip(offset, modelStack, true);
		}
		else {
			view.navigateThroughAudioOutputsForAudioClip(offset, (AudioClip*)clip, true);
		}
	}
	else if (currentUIMode == UI_MODE_NONE) {
		if (session.hasPlaybackActive()) {
			if (session.launchEventAtSwungTickCount) {
				editNumRepeatsTilLaunch(offset);
			}
			else if (offset == 1) {
				session.userWantsToArmNextSection(1);
			}
		}
	}
}

void SessionView::editNumRepeatsTilLaunch(int offset) {
	session.numRepeatsTilLaunch += offset;
	if (session.numRepeatsTilLaunch < 1) session.numRepeatsTilLaunch = 1;
	else if (session.numRepeatsTilLaunch > 9999) session.numRepeatsTilLaunch = 9999;
	else {
#if HAVE_OLED
		renderUIsForOled();
#else
		redrawNumericDisplay();
#endif
	}
}

int SessionView::horizontalEncoderAction(int offset) {
	// So long as we're not in a submode...
	if (isNoUIModeActive()) {

		// Or, if the shift key is pressed
		if (Buttons::isShiftButtonPressed()) {
			// Tell the user why they can't resize
			IndicatorLEDs::indicateAlertOnLed(clipViewLedX, clipViewLedY);
			return ACTION_RESULT_DEALT_WITH;
		}
	}

	return ClipNavigationTimelineView::horizontalEncoderAction(offset);
}

int SessionView::verticalEncoderAction(int offset, bool inCardRoutine) {

	if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW
	    || currentUIMode == UI_MODE_VIEWING_RECORD_ARMING) {

		if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine)
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.

		return verticalScrollOneSquare(offset);
	}

	return ACTION_RESULT_DEALT_WITH;
}

int SessionView::verticalScrollOneSquare(int direction) {

	if (direction == 1) {
		if (currentSong->songViewYScroll >= currentSong->sessionClips.getNumElements() - 1)
			return ACTION_RESULT_DEALT_WITH;
	}
	else {
		if (currentSong->songViewYScroll <= 1 - displayHeight) return ACTION_RESULT_DEALT_WITH;
	}

	// Drag Clip along with scroll if one is selected
	if (isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)) {

		performActionOnPadRelease = false;

		// Not allowed if recording arrangement
		if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
			numericDriver.displayPopup(HAVE_OLED ? "Recording to arrangement" : "CANT");
			return ACTION_RESULT_DEALT_WITH;
		}

		int oldIndex = selectedClipYDisplay + currentSong->songViewYScroll;

		if (direction == 1) {
			if (oldIndex >= currentSong->sessionClips.getNumElements() - 1) return ACTION_RESULT_DEALT_WITH;
		}
		else {
			if (oldIndex <= 0) return ACTION_RESULT_DEALT_WITH;
		}

		if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

		actionLogger.deleteAllLogs();

		int newIndex = oldIndex + direction;
		currentSong->sessionClips.swapElements(newIndex, oldIndex);
	}

	currentSong->songViewYScroll += direction;
	redrawClipsOnScreen();

	if (isUIModeActive(UI_MODE_VIEWING_RECORD_ARMING)) PadLEDs::reassessGreyout(true);

	return ACTION_RESULT_DEALT_WITH;
}

bool SessionView::renderSidebar(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
                                uint8_t occupancyMask[][displayWidth + sideBarWidth]) {
	if (!image) return true;

	for (int i = 0; i < displayHeight; i++) {
		if (whichRows & (1 << i)) {
			drawStatusSquare(i, image[i]);
			drawSectionSquare(i, image[i]);
		}
	}

	return true;
}

void SessionView::drawStatusSquare(uint8_t yDisplay, uint8_t thisImage[][3]) {
	uint8_t* thisColour = thisImage[displayWidth];

	Clip* clip = getClipOnScreen(yDisplay);

	// If no Clip, black
	if (!clip) memset(thisColour, 0, 3);
	else view.getClipMuteSquareColour(clip, thisColour);
}

void SessionView::drawSectionSquare(uint8_t yDisplay, uint8_t thisImage[][3]) {
	uint8_t* thisColour = thisImage[displayWidth + 1];

	Clip* clip = getClipOnScreen(yDisplay);

	// If no Clip, black
	if (!clip) memset(thisColour, 0, 3);
	else {
		if (view.midiLearnFlashOn && currentSong->sections[clip->section].launchMIDICommand.containsSomething()) {
			thisColour[0] = midiCommandColourRed;
			thisColour[1] = midiCommandColourGreen;
			thisColour[2] = midiCommandColourBlue;
		}

		else {
			hueToRGB(defaultClipGroupColours[clip->section], thisColour);

			// If user assigning MIDI controls and has this section selected, flash to half brightness
			if (view.midiLearnFlashOn && currentSong
			    && view.learnedThing == &currentSong->sections[clip->section].launchMIDICommand) {
				thisColour[0] >>= 1;
				thisColour[1] >>= 1;
				thisColour[2] >>= 1;
			}
		}
	}
}

// Will now look in subfolders too if need be.
int setPresetOrNextUnlaunchedOne(InstrumentClip* clip, int instrumentType, bool* instrumentAlreadyInSong) {
	ReturnOfConfirmPresetOrNextUnlaunchedOne result;
	result.error = Browser::currentDir.set(getInstrumentFolder(instrumentType));
	if (result.error) return result.error;

	result = Browser::findAnUnlaunchedPresetIncludingWithinSubfolders(currentSong, instrumentType,
	                                                                  AVAILABILITY_INSTRUMENT_UNUSED);
	if (result.error) return result.error;

	Instrument* newInstrument = result.fileItem->instrument;
	bool isHibernating = newInstrument && !result.fileItem->instrumentAlreadyInSong;
	*instrumentAlreadyInSong = newInstrument && result.fileItem->instrumentAlreadyInSong;

	if (!newInstrument) {
		String newPresetName;
		result.fileItem->getDisplayNameWithoutExtension(&newPresetName);
		result.error =
		    storageManager.loadInstrumentFromFile(currentSong, NULL, instrumentType, false, &newInstrument,
		                                          &result.fileItem->filePointer, &newPresetName, &Browser::currentDir);
	}

	Browser::emptyFileItems();

	if (result.error) return result.error;

	if (isHibernating) currentSong->removeInstrumentFromHibernationList(newInstrument);

#if HAVE_OLED
	OLED::displayWorkingAnimation("Loading");
#else
	numericDriver.displayLoadingAnimation();
#endif
	newInstrument->loadAllAudioFiles(true);

#if HAVE_OLED
	OLED::removeWorkingAnimation();
#endif

	result.error = clip->setAudioInstrument(newInstrument, currentSong, true, NULL); // Does a setupPatching()
	if (result.error) {
		// TODO: needs more thought - we'd want to deallocate the Instrument...
		return result.error;
	}

	if (instrumentType == INSTRUMENT_TYPE_KIT) {

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithSong(modelStackMemory, currentSong)->addTimelineCounter(clip);

		clip->assignDrumsToNoteRows(modelStack); // Does a setupPatching() for each Drum
		clip->yScroll = 0;
	}

	return NO_ERROR;
}

Clip* SessionView::createNewInstrumentClip(int yDisplay) {

	actionLogger.deleteAllLogs();

	void* memory = generalMemoryAllocator.alloc(sizeof(InstrumentClip), NULL, false, true);
	if (!memory) {
		numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
		return NULL;
	}

	InstrumentClip* newClip = new (memory) InstrumentClip(currentSong);

	unsigned int currentDisplayLength = currentSong->xZoom[NAVIGATION_CLIP] * displayWidth;

	if (playbackHandler.playbackState
	    && (currentPlaybackMode == &arrangement || !playbackHandler.isEitherClockActive())) {
		newClip->activeIfNoSolo = false;
	}

	uint32_t oneBar = currentSong->getBarLength();

	// Default Clip length. Default to current zoom, minimum 1 bar
	int32_t newClipLength = getMax(currentDisplayLength, oneBar);

	newClip->colourOffset = random(72);
	newClip->loopLength = newClipLength;

	bool instrumentAlreadyInSong;

	int instrumentType = INSTRUMENT_TYPE_SYNTH;
doGetInstrument:
	int error = setPresetOrNextUnlaunchedOne(newClip, instrumentType, &instrumentAlreadyInSong);
	if (error) {

		// If that was for a synth and there were none, try a kit
		if (error == ERROR_NO_FURTHER_PRESETS && instrumentType == INSTRUMENT_TYPE_SYNTH) {
			instrumentType = INSTRUMENT_TYPE_KIT;
			goto doGetInstrument;
		}
		newClip->~InstrumentClip();
		generalMemoryAllocator.dealloc(memory);
		numericDriver.displayError(error);
		return NULL;
	}

	int index = yDisplay + currentSong->songViewYScroll;
	if (index <= 0) {
		index = 0;
		newClip->section = currentSong->sessionClips.getClipAtIndex(0)->section;
		currentSong->songViewYScroll++;
	}
	else if (index >= currentSong->sessionClips.getNumElements()) {
		index = currentSong->sessionClips.getNumElements();
		newClip->section =
		    currentSong->sessionClips.getClipAtIndex(currentSong->sessionClips.getNumElements() - 1)->section;
	}
	currentSong->sessionClips.insertClipAtIndex(newClip, index);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(newClip);

	// Figure out the play pos for the new Clip if we're currently playing
	if (session.hasPlaybackActive() && playbackHandler.isEitherClockActive() && currentSong->isClipActive(newClip)) {
		session.reSyncClip(modelStackWithTimelineCounter, true);
	}

	if (!instrumentAlreadyInSong) currentSong->addOutput(newClip->output);

	// Possibly want to set this as the active Clip...
	if (!newClip->output->activeClip) {
		newClip->output->setActiveClip(modelStackWithTimelineCounter);
	}

	return newClip;
}

void SessionView::replaceAudioClipWithInstrumentClip(int instrumentType) {

	Clip* oldClip = getClipOnScreen(selectedClipYDisplay);

	if (!oldClip || oldClip->type != CLIP_TYPE_AUDIO) return;

	AudioClip* audioClip = (AudioClip*)oldClip;
	if (audioClip->sampleHolder.audioFile || audioClip->getCurrentlyRecordingLinearly()) {
		numericDriver.displayPopup(HAVE_OLED ? "Clip not empty" : "CANT");
		return;
	}

	// Allocate memory for InstrumentClip
	void* clipMemory = generalMemoryAllocator.alloc(sizeof(InstrumentClip), NULL, false, true);
	if (!clipMemory) {
ramError:
		numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
		return;
	}

	// Create the audio clip and ParamManager
	InstrumentClip* newClip = new (clipMemory) InstrumentClip(currentSong);

	// Give the new clip its stuff
	newClip->cloneFrom(oldClip);
	newClip->colourOffset = random(72);

	bool instrumentAlreadyInSong;
	int error;

	if (instrumentType == INSTRUMENT_TYPE_SYNTH || instrumentType == INSTRUMENT_TYPE_KIT) {

		error = setPresetOrNextUnlaunchedOne(newClip, instrumentType, &instrumentAlreadyInSong);
		if (error) {
gotError:
			numericDriver.displayError(error);
gotErrorDontDisplay:
			newClip->~InstrumentClip();
			generalMemoryAllocator.dealloc(clipMemory);
			return;
		}
	}

	else {
		Instrument* newInstrument = currentSong->getNonAudioInstrumentToSwitchTo(
		    instrumentType, AVAILABILITY_INSTRUMENT_UNUSED, 0, -1, &instrumentAlreadyInSong);
		if (!newInstrument) goto gotErrorDontDisplay;

		error = newClip->setNonAudioInstrument(newInstrument, currentSong);
		if (error) {
			// TODO: we'd really want to deallocate the Instrument
			goto gotError;
		}
	}

	if (!instrumentAlreadyInSong) currentSong->addOutput(newClip->output);

	// Possibly want to set this as the active Clip...
	if (!newClip->output->activeClip) {

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(newClip);

		newClip->output->setActiveClip(modelStackWithTimelineCounter);
	}

	currentSong->swapClips(newClip, oldClip, selectedClipYDisplay + currentSong->songViewYScroll);

	view.setActiveModControllableTimelineCounter(newClip);
	view.displayOutputName(newClip->output, true, newClip);

#if HAVE_OLED
	OLED::sendMainImage();
#endif
}

void SessionView::replaceInstrumentClipWithAudioClip() {
	Clip* oldClip = getClipOnScreen(selectedClipYDisplay);

	if (!oldClip || oldClip->type != CLIP_TYPE_INSTRUMENT) return;

	InstrumentClip* instrumentClip = (InstrumentClip*)oldClip;
	if (instrumentClip->containsAnyNotes() || instrumentClip->output->clipHasInstance(oldClip)) {
		numericDriver.displayPopup(HAVE_OLED ? "Clip not empty" : "CANT");
		return;
	}

	Clip* newClip =
	    currentSong->replaceInstrumentClipWithAudioClip(oldClip, selectedClipYDisplay + currentSong->songViewYScroll);

	if (!newClip) {
		numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
		return;
	}

	currentSong->arrangementYScroll--; // Is our best bet to avoid the scroll appearing to change visually

	view.setActiveModControllableTimelineCounter(newClip);
	view.displayOutputName(newClip->output, true, newClip);

#if HAVE_OLED
	OLED::sendMainImage();
#endif
	uiNeedsRendering(this, 1 << selectedClipYDisplay,
	                 1 << selectedClipYDisplay); // If Clip was in keyboard view, need to redraw that
}

void SessionView::removeClip(uint8_t yDisplay) {
	currentSong->ensureAllInstrumentsHaveAClipOrBackedUpParamManager(
	    "E373", "H373"); // Trying to narrow down H067 that Leo got, below.

	int clipIndex = yDisplay + currentSong->songViewYScroll;

	Clip* clip = getClipOnScreen(yDisplay);

	if (!clip) return;

	// If last session Clip left, just don't allow. Easiest
	if (currentSong->sessionClips.getNumElements() == 1) {
		numericDriver.displayPopup(HAVE_OLED ? "Can't remove final clip" : "LAST");
		return;
	}

	// If this Clip is the inputTickScaleClip
	if (clip == currentSong->getSyncScalingClip()) {
		// Don't let the user do it
		IndicatorLEDs::indicateAlertOnLed(syncScalingLedX, syncScalingLedY);
		return;
	}

	clip->stopAllNotesPlaying(currentSong); // Stops any MIDI-controlled auditioning / stuck notes

	currentSong->removeSessionClip(clip, clipIndex);

	if (playbackHandler.isEitherClockActive() && currentPlaybackMode == &session) {
		session.launchSchedulingMightNeedCancelling();
	}

	redrawClipsOnScreen();

	currentSong->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E067", "H067"); // Leo got a H067!!!!
}

Clip* SessionView::getClipOnScreen(int yDisplay) {
	int index = yDisplay + currentSong->songViewYScroll;

	if (index < 0 || index >= currentSong->sessionClips.getNumElements()) return NULL;

	return currentSong->sessionClips.getClipAtIndex(index);
}

void SessionView::redrawClipsOnScreen(bool doRender) {
	if (doRender) {
		uiNeedsRendering(this);
	}
	view.flashPlayEnable();
}

void SessionView::setLedStates() {

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	IndicatorLEDs::setLedState(keyboardLedX, keyboardLedY, false);
#endif

	view.setLedStates();

#ifdef currentClipStatusButtonX
	view.switchOffCurrentClipPad();
#endif
}

#if HAVE_OLED

extern char loopsRemainingText[];

void SessionView::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {

	if (playbackHandler.isEitherClockActive()) {
		// Session playback
		if (currentPlaybackMode == &session) {
			if (session.launchEventAtSwungTickCount) {
yesDoIt:
				intToString(session.numRepeatsTilLaunch, &loopsRemainingText[17]);
				OLED::drawPermanentPopupLookingText(loopsRemainingText);
			}
		}

		else { // Arrangement playback
			if (playbackHandler.stopOutputRecordingAtLoopEnd) {
				OLED::drawPermanentPopupLookingText("Resampling will end...");
			}
		}
	}
}

#else

void SessionView::redrawNumericDisplay() {

	if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) return;

	// If playback on...
	if (playbackHandler.isEitherClockActive()) {

		// Session playback
		if (currentPlaybackMode == &session) {
			if (!session.launchEventAtSwungTickCount) goto nothingToDisplay;

			if (getCurrentUI() == &loadSongUI) {
				if (currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
yesDoIt:
					char buffer[5];
					intToString(session.numRepeatsTilLaunch, buffer);
					numericDriver.setText(buffer, true, 255, true, NULL, false, true);
				}
			}

			else if (getCurrentUI() == &arrangerView) {
				if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW
				    || currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
					if (session.switchToArrangementAtLaunchEvent) goto yesDoIt;
					else goto setBlank;
				}
			}

			else if (getCurrentUI() == this) {
				if (currentUIMode != UI_MODE_HOLDING_SECTION_PAD) {
					goto yesDoIt;
				}
			}
		}

		else { // Arrangement playback
			if (getCurrentUI() == &arrangerView) {

				if (currentUIMode != UI_MODE_HOLDING_SECTION_PAD && currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW) {
					if (playbackHandler.stopOutputRecordingAtLoopEnd) {
						numericDriver.setText("1", true, 255, true, NULL, false, true);
					}
					else goto setBlank;
				}
			}
			else if (getCurrentUI() == this) {
setBlank:
				numericDriver.setText("");
			}
		}
	}

	// Or if no playback active...
	else {
nothingToDisplay:
		if (getCurrentUI() == this || getCurrentUI() == &arrangerView) {
			if (currentUIMode != UI_MODE_HOLDING_SECTION_PAD) {
				numericDriver.setText("");
			}
		}
	}

	setCentralLEDStates();
}
#endif

// This gets called by redrawNumericDisplay() - or, if HAVE_OLED, it gets called instead, because this still needs to happen.
void SessionView::setCentralLEDStates() {
	IndicatorLEDs::setLedState(synthLedX, synthLedY, false);
	IndicatorLEDs::setLedState(kitLedX, kitLedY, false);
	IndicatorLEDs::setLedState(midiLedX, midiLedY, false);
	IndicatorLEDs::setLedState(cvLedX, cvLedY, false);
	IndicatorLEDs::setLedState(scaleModeLedX, scaleModeLedY, false);
#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	IndicatorLEDs::setLedState(keyboardLedX, keyboardLedY, false);
#endif

	if (getCurrentUI() == this) IndicatorLEDs::setLedState(crossScreenEditLedX, crossScreenEditLedY, false);
}

unsigned int SessionView::getMaxZoom() {
	return currentSong->getLongestClip(true, false)->getMaxZoom();
}

void SessionView::cloneClip(uint8_t yDisplayFrom, uint8_t yDisplayTo) {
	Clip* clipToClone = getClipOnScreen(yDisplayFrom);
	if (!clipToClone) return;

	// Just don't allow cloning of Clips which are linearly recording
	if (clipToClone->getCurrentlyRecordingLinearly()) {
		numericDriver.displayPopup(HAVE_OLED ? "Recording in progress" : "CANT");
		return;
	}

	bool enoughSpace = currentSong->sessionClips.ensureEnoughSpaceAllocated(1);
	if (!enoughSpace) {
ramError:
		numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
		return;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithSong(modelStackMemory, currentSong)->addTimelineCounter(clipToClone);

	int error = clipToClone->clone(modelStack);
	if (error) goto ramError;

	Clip* newClip = (Clip*)modelStack->getTimelineCounter();

	newClip->section = (uint8_t)(newClip->section + 1) % MAX_NUM_SECTIONS;

	int newIndex = yDisplayTo + currentSong->songViewYScroll;

	if (yDisplayTo < yDisplayFrom) {
		currentSong->songViewYScroll++;
		newIndex++;
	}

	if (newIndex < 0) newIndex = 0;
	else if (newIndex > currentSong->sessionClips.getNumElements()) {
		newIndex = currentSong->sessionClips.getNumElements();
	}

	currentSong->sessionClips.insertClipAtIndex(newClip, newIndex); // Can't fail - we ensured enough space in advance

	redrawClipsOnScreen();
}

void SessionView::graphicsRoutine() {

	uint8_t tickSquares[displayHeight];
	uint8_t colours[displayHeight];

	bool anyLinearRecordingOnThisScreen = false;
	bool anyLinearRecordingOnNextScreen = false;

	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		int newTickSquare;

		Clip* clip = getClipOnScreen(yDisplay);

		if (!playbackHandler.playbackState || !clip || !currentSong->isClipActive(clip)
		    || playbackHandler.ticksLeftInCountIn || currentUIMode == UI_MODE_HORIZONTAL_ZOOM
		    || (currentUIMode == UI_MODE_HORIZONTAL_SCROLL && PadLEDs::transitionTakingPlaceOnRow[yDisplay])) {
			newTickSquare = 255;
		}

		// Tempoless recording
		else if (!playbackHandler.isEitherClockActive()) {
			newTickSquare = displayWidth - 1;

			if (clip->getCurrentlyRecordingLinearly()) { // This would have to be true if we got here, I think?
				if (clip->type == CLIP_TYPE_AUDIO) {
					((AudioClip*)clip)->renderData.xScroll = -1; // Make sure values are recalculated

					rowNeedsRenderingDependingOnSubMode(yDisplay);
				}
				colours[yDisplay] = 2;
			}
		}
		else {
			int localScroll =
			    getClipLocalScroll(clip, currentSong->xScroll[NAVIGATION_CLIP], currentSong->xZoom[NAVIGATION_CLIP]);
			Clip* clipToRecordTo = clip->getClipToRecordTo();
			int32_t livePos = clipToRecordTo->getLivePos();

			// If we are recording to another Clip, we have to use its position.
			if (clipToRecordTo != clip) {
				int whichRepeat = (uint32_t)livePos / (uint32_t)clip->loopLength;
				livePos -= whichRepeat * clip->loopLength;

				// But if it's currently reversing, we have to re-apply that here.
				if (clip->sequenceDirectionMode == SEQUENCE_DIRECTION_REVERSE
				    || (clip->sequenceDirectionMode == SEQUENCE_DIRECTION_PINGPONG && (whichRepeat & 1))) {
					livePos = -livePos;
					if (livePos < 0) livePos += clip->loopLength;
				}
			}

			newTickSquare = getSquareFromPos(livePos, NULL, localScroll);

			// Linearly recording
			if (clip->getCurrentlyRecordingLinearly()) {
				if (clip->type == CLIP_TYPE_AUDIO) {
					if (currentUIMode != UI_MODE_HORIZONTAL_SCROLL && currentUIMode != UI_MODE_HORIZONTAL_ZOOM) {
						rowNeedsRenderingDependingOnSubMode(yDisplay);
					}
				}

				if (newTickSquare >= 0
				    && (!clip->armState
				        || xScrollBeforeFollowingAutoExtendingLinearRecording
				               != -1)) { // Only if it's auto extending, or it was before
					if (newTickSquare < displayWidth) {
						anyLinearRecordingOnThisScreen = true;
					}
					else if (newTickSquare == displayWidth) {
						anyLinearRecordingOnNextScreen = true;
					}
				}

				colours[yDisplay] = 2;
			}

			// Not linearly recording
			else {
				colours[yDisplay] = 0;
			}

			if (newTickSquare < 0 || newTickSquare >= displayWidth) newTickSquare = 255;
		}

		tickSquares[yDisplay] = newTickSquare;
	}

	// Auto scrolling for linear recording --------

	// If no linear recording onscreen now...
	if (!anyLinearRecordingOnThisScreen && currentUIMode != UI_MODE_HORIZONTAL_SCROLL) {

		// If there's some on the next screen to the right, go there
		if (anyLinearRecordingOnNextScreen) {

			if (!currentUIMode && getCurrentUI() == this) {

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
	}

	PadLEDs::setTickSquares(tickSquares, colours);
}

void SessionView::rowNeedsRenderingDependingOnSubMode(int yDisplay) {

	switch (currentUIMode) {
	case UI_MODE_HORIZONTAL_SCROLL:
	case UI_MODE_HORIZONTAL_ZOOM:
	case UI_MODE_AUDIO_CLIP_EXPANDING:
	case UI_MODE_AUDIO_CLIP_COLLAPSING:
	case UI_MODE_INSTRUMENT_CLIP_EXPANDING:
	case UI_MODE_INSTRUMENT_CLIP_COLLAPSING:
	case UI_MODE_ANIMATION_FADE:
	case UI_MODE_EXPLODE_ANIMATION:
		break;

	default:
		uiNeedsRendering(this, 1 << yDisplay, 0);
	}
}

bool SessionView::calculateZoomPinSquares(uint32_t oldScroll, uint32_t newScroll, uint32_t newZoom, uint32_t oldZoom) {

	bool anyToDo = false;

	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {

		Clip* clip = getClipOnScreen(yDisplay);

		if (clip && clip->currentlyScrollableAndZoomable()) {
			int32_t oldLocal = getClipLocalScroll(clip, oldScroll, oldZoom);
			int32_t newLocal = getClipLocalScroll(clip, newScroll, newZoom);

			PadLEDs::zoomPinSquare[yDisplay] =
			    ((int64_t)(int32_t)(oldLocal - newLocal) << 16) / (int32_t)(newZoom - oldZoom);
			PadLEDs::transitionTakingPlaceOnRow[yDisplay] = true;
			anyToDo = true;
		}
		else PadLEDs::transitionTakingPlaceOnRow[yDisplay] = false;
	}

	return anyToDo;
}

int SessionView::getClipPlaceOnScreen(Clip* clip) {
	return currentSong->sessionClips.getIndexForClip(clip) - currentSong->songViewYScroll;
}

uint32_t SessionView::getMaxLength() {
	return currentSong->getLongestClip(true, false)->loopLength;
}

extern bool pendingUIRenderingLock;

bool SessionView::setupScroll(uint32_t oldScroll) {

	// Ok I'm sorta pretending that this is definitely previously false, though only one caller of this function actually
	// checks for that. Should be ok-ish though...
	pendingUIRenderingLock = true;

	uint32_t xZoom = currentSong->xZoom[NAVIGATION_CLIP];

	bool anyMoved = false;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {

		Clip* clip = getClipOnScreen(yDisplay);

		if (clip && clip->currentlyScrollableAndZoomable()) {

			uint32_t newLocalPos = getClipLocalScroll(clip, currentSong->xScroll[NAVIGATION_CLIP], xZoom);
			uint32_t oldLocalPos = getClipLocalScroll(clip, oldScroll, xZoom);
			bool moved = (newLocalPos != oldLocalPos);
			if (moved) {
				ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

				clip->renderAsSingleRow(modelStackWithTimelineCounter, this, newLocalPos, xZoom,
				                        PadLEDs::imageStore[yDisplay][0], PadLEDs::occupancyMaskStore[yDisplay]);
				anyMoved = true;
			}
			PadLEDs::transitionTakingPlaceOnRow[yDisplay] = moved;
		}
		else {
noTransition:
			PadLEDs::transitionTakingPlaceOnRow[yDisplay] = false;
		}
	}

	pendingUIRenderingLock = false;

	return anyMoved;
}

uint32_t SessionView::getClipLocalScroll(Clip* clip, uint32_t overviewScroll, uint32_t xZoom) {
	return getMin((clip->loopLength - 1) / (xZoom * displayWidth) * xZoom * displayWidth, overviewScroll);
}

void SessionView::flashPlayRoutine() {
	view.clipArmFlashOn = !view.clipArmFlashOn;
	uint32_t whichRowsNeedReRendering = 0;

	bool any = false;
	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		Clip* clip = getClipOnScreen(yDisplay);
		if (clip && clip->armState) {
			whichRowsNeedReRendering |= (1 << yDisplay);
		}
	}
	if (whichRowsNeedReRendering) {
		view.flashPlayEnable();
		uiNeedsRendering(this, 0, whichRowsNeedReRendering);
	}
}

void SessionView::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	UI::modEncoderButtonAction(whichModEncoder, on);
	performActionOnPadRelease = false;
}

void SessionView::modButtonAction(uint8_t whichButton, bool on) {
	UI::modButtonAction(whichButton, on);
	performActionOnPadRelease = false;
}

void SessionView::noteRowChanged(InstrumentClip* instrumentClip, NoteRow* noteRow) {

	if (currentUIMode == UI_MODE_HORIZONTAL_SCROLL)
		return; // Is this 100% correct? What if that one Clip isn't visually scrolling?

	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		Clip* clip = getClipOnScreen(yDisplay);
		if (clip == instrumentClip) {
			uiNeedsRendering(this, 1 << yDisplay, 0);
			return;
		}
	}
}

uint32_t SessionView::getGreyedOutRowsNotRepresentingOutput(Output* output) {
	uint32_t rows = 0xFFFFFFFF;
	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		Clip* clip = getClipOnScreen(yDisplay);
		if (clip && clip->output == output) {
			rows &= ~(1 << yDisplay);
		}
	}
	return rows;
}

bool SessionView::renderMainPads(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
                                 uint8_t occupancyMask[][displayWidth + sideBarWidth], bool drawUndefinedArea) {
	if (!image) return true;

	uint32_t whichRowsCouldntBeRendered = 0;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	PadLEDs::renderingLock = true;

	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		if (whichRows & (1 << yDisplay)) {
			bool success = renderRow(modelStack, yDisplay, image[yDisplay], occupancyMask[yDisplay], drawUndefinedArea);
			if (!success) {
				whichRowsCouldntBeRendered |= (1 << yDisplay);
			}
		}
	}
	PadLEDs::renderingLock = false;

	if (whichRowsCouldntBeRendered && image == PadLEDs::image) uiNeedsRendering(this, whichRowsCouldntBeRendered, 0);

	return true;
}

// Returns false if can't because in card routine
bool SessionView::renderRow(ModelStack* modelStack, uint8_t yDisplay, uint8_t thisImage[displayWidth + sideBarWidth][3],
                            uint8_t thisOccupancyMask[displayWidth + sideBarWidth], bool drawUndefinedArea) {

	Clip* clip = getClipOnScreen(yDisplay);

	if (clip) {

		// If user assigning MIDI controls and this Clip has a command assigned, flash pink
		if (view.midiLearnFlashOn
		    && (clip->output->type == INSTRUMENT_TYPE_SYNTH || clip->output->type == INSTRUMENT_TYPE_MIDI_OUT
		        || clip->output->type == INSTRUMENT_TYPE_CV)
		    && ((MelodicInstrument*)clip->output)->midiInput.containsSomething()) {

			for (int xDisplay = 0; xDisplay < displayWidth; xDisplay++) {
				// We halve the intensity of the brightness in this case, because a lot of pads will be lit, it looks mental, and I think one user was having it
				// cause his Deluge to freeze due to underpowering.
				thisImage[xDisplay][0] = midiCommandColourRed >> 1;
				thisImage[xDisplay][1] = midiCommandColourGreen >> 1;
				thisImage[xDisplay][2] = midiCommandColourBlue >> 1;
			}
		}

		else {

			bool success = true;

			if (clip->isPendingOverdub) {
				for (int xDisplay = 0; xDisplay < displayWidth; xDisplay++) {
					thisImage[xDisplay][0] = 30;
					thisImage[xDisplay][1] = 0;
					thisImage[xDisplay][2] = 0;
				}
			}
			else {
				ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

				success = clip->renderAsSingleRow(modelStackWithTimelineCounter, this,
				                                  getClipLocalScroll(clip, currentSong->xScroll[NAVIGATION_CLIP],
				                                                     currentSong->xZoom[NAVIGATION_CLIP]),
				                                  currentSong->xZoom[NAVIGATION_CLIP], thisImage[0], thisOccupancyMask,
				                                  drawUndefinedArea);
			}

			if (view.thingPressedForMidiLearn == MIDI_LEARN_MELODIC_INSTRUMENT_INPUT && view.midiLearnFlashOn
			    && view.learnedThing
			           == &((MelodicInstrument*)clip->output)
			                   ->midiInput) { // Should be fine even if output isn't a MelodicInstrument

				for (int xDisplay = 0; xDisplay < displayWidth; xDisplay++) {
					thisImage[xDisplay][0] >>= 1;
					thisImage[xDisplay][1] >>= 1;
					thisImage[xDisplay][2] >>= 1;
				}
			}

			return success;
		}
	}
	else {
		memset(thisImage, 0, displayWidth * 3);
		// Occupancy mask doesn't need to be cleared in this case
	}

	return true;
}

void SessionView::transitionToViewForClip(Clip* clip) {

	// If no Clip, just go back into the previous one we were in
	if (!clip) {
		clip = currentSong->currentClip;

		// If there was no previous one (e.g. because we just loaded the Song), do nothing.
		if (!clip || clip->section == 255) {
			return;
		}
	}
	currentSong->currentClip = clip;
	int clipPlaceOnScreen = getMax((int16_t)-1, getMin((int16_t)displayHeight, getClipPlaceOnScreen(clip)));

	currentSong->xScroll[NAVIGATION_CLIP] =
	    getClipLocalScroll(clip, currentSong->xScroll[NAVIGATION_CLIP], currentSong->xZoom[NAVIGATION_CLIP]);

	PadLEDs::recordTransitionBegin(clipCollapseSpeed);

	// InstrumentClips
	if (clip->type == CLIP_TYPE_INSTRUMENT) {

		currentUIMode = UI_MODE_INSTRUMENT_CLIP_EXPANDING;

		if (((InstrumentClip*)clip)->onKeyboardScreen) {

			keyboardScreen.recalculateColours();
			keyboardScreen.renderMainPads(0xFFFFFFFF, PadLEDs::imageStore, PadLEDs::occupancyMaskStore);

			PadLEDs::numAnimatedRows = displayHeight;
			for (int y = 0; y < PadLEDs::numAnimatedRows; y++) {
				PadLEDs::animatedRowGoingTo[y] = clipPlaceOnScreen;
				PadLEDs::animatedRowGoingFrom[y] = y;
			}
		}

		else {
			instrumentClipView
			    .recalculateColours(); // Won't have happened automatically because we haven't begun the "session"
			instrumentClipView.renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1],
			                                  false);
			instrumentClipView.renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1]);

			instrumentClipView
			    .fillOffScreenImageStores(); // Important that this is done after currentSong->xScroll is changed, above

			PadLEDs::numAnimatedRows = displayHeight + 2;
			for (int y = 0; y < PadLEDs::numAnimatedRows; y++) {
				PadLEDs::animatedRowGoingTo[y] = clipPlaceOnScreen;
				PadLEDs::animatedRowGoingFrom[y] = y - 1;
			}
		}

		PadLEDs::setupInstrumentClipCollapseAnimation(true);

		PadLEDs::renderClipExpandOrCollapse();
	}

	// AudioClips
	else {

		AudioClip* clip = (AudioClip*)currentSong->currentClip;

		Sample* sample = (Sample*)clip->sampleHolder.audioFile;

		if (sample) {

			currentUIMode = UI_MODE_AUDIO_CLIP_EXPANDING;

			waveformRenderer.collapseAnimationToWhichRow = clipPlaceOnScreen;

			PadLEDs::setupAudioClipCollapseOrExplodeAnimation(clip);

			PadLEDs::renderAudioClipExpandOrCollapse();

			PadLEDs::clearSideBar(); // Sends "now"
		}

		// If no sample, just skip directly there
		else {
			currentUIMode = UI_MODE_NONE;
			changeRootUI(&audioClipView);
		}
	}
}

// Might be called during card routine! So renders might fail. Not too likely
void SessionView::finishedTransitioningHere() {
	AudioEngine::routineWithClusterLoading(); // -----------------------------------
	currentUIMode = UI_MODE_ANIMATION_FADE;
	PadLEDs::recordTransitionBegin(fadeSpeed);
	changeRootUI(this);
	renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[displayHeight], &PadLEDs::occupancyMaskStore[displayHeight], true);
	renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[displayHeight], &PadLEDs::occupancyMaskStore[displayHeight]);
	PadLEDs::timerRoutine(); // What... why? This would normally get called from that...
}

void SessionView::playbackEnded() {

	uint32_t whichRowsToReRender = 0;

	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		Clip* clip = getClipOnScreen(yDisplay);
		if (clip && clip->type == CLIP_TYPE_AUDIO) {
			AudioClip* audioClip = (AudioClip*)clip;

			if (!audioClip->sampleHolder.audioFile) {
				whichRowsToReRender |= (1 << yDisplay);
			}
		}
	}

	if (whichRowsToReRender) uiNeedsRendering(this, whichRowsToReRender, 0);
}

void SessionView::clipNeedsReRendering(Clip* clip) {
	int bottomIndex = currentSong->songViewYScroll;
	int topIndex = bottomIndex + displayHeight;

	bottomIndex = getMax(bottomIndex, 0);
	topIndex = getMin(topIndex, currentSong->sessionClips.getNumElements());

	for (int c = bottomIndex; c < topIndex; c++) {
		Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);
		if (thisClip == clip) {
			int yDisplay = c - currentSong->songViewYScroll;
			uiNeedsRendering(this, (1 << yDisplay), 0);
			break;
		}
	}
}

void SessionView::sampleNeedsReRendering(Sample* sample) {
	int bottomIndex = currentSong->songViewYScroll;
	int topIndex = bottomIndex + displayHeight;

	bottomIndex = getMax(bottomIndex, 0);
	topIndex = getMin(topIndex, currentSong->sessionClips.getNumElements());

	for (int c = bottomIndex; c < topIndex; c++) {
		Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);
		if (thisClip->type == CLIP_TYPE_AUDIO && ((AudioClip*)thisClip)->sampleHolder.audioFile == sample) {
			int yDisplay = c - currentSong->songViewYScroll;
			uiNeedsRendering(this, (1 << yDisplay), 0);
		}
	}
}

void SessionView::midiLearnFlash() {

	uint32_t mainRowsToRender = 0;
	uint32_t sideRowsToRender = 0;

	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		Clip* clip = getClipOnScreen(yDisplay);
		if (clip) {

			if (clip->muteMIDICommand.containsSomething()
			    || (view.thingPressedForMidiLearn == MIDI_LEARN_CLIP && &clip->muteMIDICommand == view.learnedThing)
			    || currentSong->sections[clip->section].launchMIDICommand.containsSomething()
			    || (view.thingPressedForMidiLearn == MIDI_LEARN_SECTION
			        && view.learnedThing == &currentSong->sections[clip->section].launchMIDICommand)) {
				sideRowsToRender |= (1 << yDisplay);
			}

			if (clip->output->type == INSTRUMENT_TYPE_SYNTH || clip->output->type == INSTRUMENT_TYPE_MIDI_OUT
			    || clip->output->type == INSTRUMENT_TYPE_CV) {

				if (((MelodicInstrument*)clip->output)->midiInput.containsSomething()
				    || (view.thingPressedForMidiLearn == MIDI_LEARN_MELODIC_INSTRUMENT_INPUT
				        && view.learnedThing
				               == &((MelodicInstrument*)clip->output)
				                       ->midiInput)) { // Should be fine even if output isn't a MelodicInstrument

					mainRowsToRender |= (1 << yDisplay);
				}
			}
		}
	}

	uiNeedsRendering(this, mainRowsToRender, sideRowsToRender);
}

void SessionView::modEncoderAction(int whichModEncoder, int offset) {
	performActionOnPadRelease = false;

	ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);
}
