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

#include "gui/views/session_view.h"
#include "definitions_cxx.hpp"
#include "dsp/master_compressor/master_compressor.h"
#include "extern.h"
#include "gui/colour.h"
#include "gui/context_menu/audio_input_selector.h"
#include "gui/context_menu/launch_style.h"
#include "gui/menu_item/colour.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation_instrument_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_session_view.h"
#include "gui/views/view.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/print.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/instrument/instrument.h"
#include "model/instrument/melodic_instrument.h"
#include "model/note/note_row.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/d_string.h"
#include "util/functions.h"
#include <algorithm>
#include <cstdint>
#include <new>

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "util/cfunctions.h"
}

using namespace deluge;
using namespace gui;

SessionView sessionView{};

SessionView::SessionView() {
	xScrollBeforeFollowingAutoExtendingLinearRecording = -1;
}

bool SessionView::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING) {
		switch (currentSong->sessionLayout) {
		case SessionLayoutType::SessionLayoutTypeRows: {
			*cols = 0xFFFFFFFD;
			*rows = 0;
			for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
				Clip* clip = getClipOnScreen(yDisplay);
				if (clip && !clip->armedForRecording) {
					*rows |= (1 << yDisplay);
				}
			}
			break;
		}
		case SessionLayoutType::SessionLayoutTypeGrid: {
			*cols = 0x03; // Only sidebar
			*rows = 0x0;
			break;
		}
		}

		return true;
	}
	else if (playbackHandler.playbackState && currentPlaybackMode == &arrangement) {
		*cols = 0b11;
		return true;
	}
	else {
		return false;
	}
}

bool SessionView::opened() {
	if (playbackHandler.playbackState && currentPlaybackMode == &arrangement) {
		PadLEDs::skipGreyoutFade();
	}

	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);

	focusRegained();

	return true;
}

void SessionView::focusRegained() {
	selectLayout(0); // Make sure we get a valid layout from the loaded file

	bool doingRender = (currentUIMode != UI_MODE_ANIMATION_FADE);
	redrawClipsOnScreen(doingRender); // We want this here, not just in opened(), because after coming back from
	                                  // loadInstrumentPresetUI, need to at least redraw, and also really need to
	                                  // re-render stuff in case note-tails-being-allowed has changed

	ClipNavigationTimelineView::focusRegained();
	view.focusRegained();
	view.setActiveModControllableTimelineCounter(currentSong);

	selectedClipYDisplay = 255;
	if (display->haveOLED()) {
		setCentralLEDStates();
	}
	else {
		redrawNumericDisplay();
	}

	indicator_leds::setLedState(IndicatorLED::BACK, false);

	setLedStates();

	currentSong->lastClipInstanceEnteredStartPos = -1;
}

ActionResult SessionView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	InstrumentType newInstrumentType;

	// Clip-view button
	if (b == CLIP_VIEW) {
		if (on && currentUIMode == UI_MODE_NONE && playbackHandler.recording != RECORDING_ARRANGEMENT) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			transitionToViewForClip(); // May fail if no currentClip
		}
	}

	// Song-view button without shift

	// Arranger view button, or if there isn't one then song view button
#ifdef arrangerViewButtonX
	else if (b == arrangerView) {
#else
	else if (b == SESSION_VIEW && !Buttons::isShiftButtonPressed()) {
#endif
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		bool lastSessionButtonActiveState = sessionButtonActive;
		sessionButtonActive = on;

		// Press with special modes
		if (on) {
			sessionButtonUsed = false;

			// If holding record button...
			if (Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
				Buttons::recordButtonPressUsedUp = true;

				// Make sure we weren't already playing...
				if (!playbackHandler.playbackState) {

					Action* action = actionLogger.getNewAction(ACTION_ARRANGEMENT_RECORD, false);

					arrangerView.xScrollWhenPlaybackStarted = currentSong->xScroll[NAVIGATION_ARRANGEMENT];
					if (action) {
						action->posToClearArrangementFrom = arrangerView.xScrollWhenPlaybackStarted;
					}

					currentSong->clearArrangementBeyondPos(
					    arrangerView.xScrollWhenPlaybackStarted,
					    action); // Want to do this before setting up playback or place new instances
					int32_t error =
					    currentSong->placeFirstInstancesOfActiveClips(arrangerView.xScrollWhenPlaybackStarted);

					if (error) {
						display->displayError(error);
						return ActionResult::DEALT_WITH;
					}
					playbackHandler.recording = RECORDING_ARRANGEMENT;
					playbackHandler.setupPlaybackUsingInternalClock();

					arrangement.playbackStartedAtPos =
					    arrangerView.xScrollWhenPlaybackStarted; // Have to do this after setting up playback

					indicator_leds::blinkLed(IndicatorLED::RECORD, 255, 1);
					indicator_leds::blinkLed(IndicatorLED::SESSION_VIEW, 255, 1);
					sessionButtonUsed = true;
				}
			}

			else if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
				if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
					return ActionResult::DEALT_WITH;
				}

				// Rows are not aligned in grid so we disabled this function, the code below also would need to be aligned
				if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
					display->displayPopup(l10n::get(l10n::String::STRING_FOR_IMPOSSIBLE_FROM_GRID));
					return ActionResult::DEALT_WITH;
				}

				actionLogger.deleteAllLogs();

				Clip* clip = getClipOnScreen(selectedClipYDisplay);
				Output* output = clip->output;
				int32_t instrumentIndex = currentSong->getOutputIndex(output);
				currentSong->arrangementYScroll = instrumentIndex - selectedClipPressYDisplay;

				int32_t posPressed = arrangerView.getPosFromSquare(selectedClipPressXDisplay);
				int32_t proposedStartPos = posPressed;

				int32_t i = output->clipInstances.search(proposedStartPos, LESS);
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
				if (proposedStartPos > kMaxSequenceLength - clip->loopLength) {
					display->displayPopup(
					    deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIP_WOULD_BREACH_MAX_ARRANGEMENT_LENGTH));
					return ActionResult::DEALT_WITH;
				}

				// If we're here, we're ok!
				int32_t error = output->clipInstances.insertAtIndex(i);
				if (error) {
					display->displayError(error);
					return ActionResult::DEALT_WITH;
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
				sessionButtonActive = false;
				goToArrangementEditor();
			}
		}
		// Release without special mode
		else if (!on && currentUIMode == UI_MODE_NONE) {
			if (lastSessionButtonActiveState && !sessionButtonActive && !sessionButtonUsed && !gridFirstPadActive()) {
				if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
					currentSong->endInstancesOfActiveClips(playbackHandler.getActualArrangementRecordPos());
					// Must call before calling getArrangementRecordPos(), cos that detaches the cloned Clip
					currentSong->resumeClipsClonedForArrangementRecording();
					playbackHandler.recording = RECORDING_OFF;
					view.setModLedStates();
					playbackHandler.setLedStates();
				}
				else {
					goToArrangementEditor();
				}

				sessionButtonUsed = false;
			}
		}
	}

	// Affect-entire button
	else if (b == AFFECT_ENTIRE) {
		if (on && currentUIMode == UI_MODE_NONE) {
			currentSong->affectEntire = !currentSong->affectEntire;
			view.setActiveModControllableTimelineCounter(currentSong);
		}
	}

	// Record button - adds to what MatrixDriver does with it
	else if (b == RECORD) {
		if (on) {
			if (isNoUIModeActive()) {
				uiTimerManager.setTimer(TIMER_UI_SPECIFIC, 500);
				view.blinkOn = true;
			}
			else {
				goto notDealtWith;
			}
		}
		else {
			if (isUIModeActive(UI_MODE_VIEWING_RECORD_ARMING)) {
				exitUIMode(UI_MODE_VIEWING_RECORD_ARMING);
				PadLEDs::reassessGreyout(false);
				requestRendering(this, 0, 0xFFFFFFFF);
			}
			else {
				goto notDealtWith;
			}
		}
		return ActionResult::NOT_DEALT_WITH; // Make the MatrixDriver do its normal thing with it too
	}

	// If save / delete button pressed, delete the Clip!
	else if (b == SAVE && (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW || gridFirstPadActive())) {
		if (on) {

			if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
				performActionOnPadRelease = false;
				return ActionResult::DEALT_WITH;
			}

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			Clip* clip = getClipForLayout();

			if (clip != nullptr) {
				actionLogger.deleteAllLogs();
				clipPressEnded();
				removeClip(clip);
			}
		}
	}

	// Select encoder button
	else if (b == SELECT_ENC && !Buttons::isShiftButtonPressed()) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (currentUIMode == UI_MODE_HOLDING_SECTION_PAD) {
				if (performActionOnSectionPadRelease) {
					beginEditingSectionRepeatsNum();
				}
				else {
					currentSong->sections[sectionPressed].numRepetitions = 0;
					drawSectionRepeatNumber();
				}
			}
			else if (currentUIMode == UI_MODE_HOLDING_STATUS_PAD) {
				//Clip* clip = getClipOnScreen(selectedClipYDisplay);
				//contextMenuLaunchStyle.clip = clip;
				context_menu::launchStyle.setupAndCheckAvailability();
				openUI(&context_menu::launchStyle);
			}
			else if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
				actionLogger.deleteAllLogs();
				performActionOnPadRelease = false;

				Clip* clip = getClipForLayout();
				if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
					requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
				}

				if (clip != nullptr) {
					replaceInstrumentClipWithAudioClip(clip);
				}
			}
			else if (currentUIMode == UI_MODE_NONE) {
				if (session.hasPlaybackActive()) {
					if (session.launchEventAtSwungTickCount) {
						session.cancelAllArming();
						session.cancelAllLaunchScheduling();
						session.lastSectionArmed = 255;
						if (display->haveOLED()) {
							renderUIsForOled();
						}
						else {
							redrawNumericDisplay();
						}
						requestRendering(this, 0, 0xFFFFFFFF);
					}
				}
				//open Song FX menu
				display->setNextTransitionDirection(1);
				soundEditor.setup();
				openUI(&soundEditor);
			}
		}
	}

	// Which-instrument-type buttons
	else if (b == SYNTH) {
		newInstrumentType = InstrumentType::SYNTH;

changeInstrumentType:
		if (on && currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW && !Buttons::isShiftButtonPressed()) {

			performActionOnPadRelease = false;

			if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
				return ActionResult::DEALT_WITH;
			}

			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			Clip* clip = getClipForLayout();

			if (clip != nullptr) {
				// If AudioClip, we have to convert back to an InstrumentClip
				if (clip->type == CLIP_TYPE_AUDIO) {
					actionLogger.deleteAllLogs();
					replaceAudioClipWithInstrumentClip(clip, newInstrumentType);
				}

				// Or if already an InstrumentClip, changing Instrument type is easier
				else {

					InstrumentClip* instrumentClip = (InstrumentClip*)clip;
					Instrument* instrument = (Instrument*)instrumentClip->output;

					// If load button held, go into LoadInstrumentPresetUI
					if (Buttons::isButtonPressed(deluge::hid::button::LOAD)) {

						// Can't do that for MIDI or CV Clips though
						if (newInstrumentType == InstrumentType::MIDI_OUT || newInstrumentType == InstrumentType::CV) {
							goto doActualSimpleChange;
						}

						actionLogger.deleteAllLogs();

						currentUIMode = UI_MODE_NONE;
						selectedClipYDisplay = 255;

						Browser::instrumentTypeToLoad = newInstrumentType;
						loadInstrumentPresetUI.instrumentToReplace = instrument;
						switch (currentSong->sessionLayout) {
						case SessionLayoutType::SessionLayoutTypeRows: {
							loadInstrumentPresetUI.instrumentClipToLoadFor = instrumentClip;
							break;
						}
						case SessionLayoutType::SessionLayoutTypeGrid: {
							// Not supplying an instrument will make it replace the output for all clips
							loadInstrumentPresetUI.instrumentClipToLoadFor = NULL;
							break;
						}
						}
						loadInstrumentPresetUI.loadingSynthToKitRow = false;
						openUI(&loadInstrumentPresetUI);
					}

					// Otherwise, just change the instrument type
					else {
doActualSimpleChange:

						switch (currentSong->sessionLayout) {
						case SessionLayoutType::SessionLayoutTypeRows: {
							char modelStackMemory[MODEL_STACK_MAX_SIZE];
							ModelStackWithTimelineCounter* modelStack =
							    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, instrumentClip);

							view.changeInstrumentType(newInstrumentType, modelStack, true);
							break;
						}
						case SessionLayoutType::SessionLayoutTypeGrid: {
							// Mostly taken from ArrangerView::changeInstrumentType
							if (instrument->type != newInstrumentType) {
								Instrument* newInstrument =
								    currentSong->changeInstrumentType(instrument, newInstrumentType);
								if (newInstrument) {
									view.displayOutputName(newInstrument);
									if (display->haveOLED()) {
										deluge::hid::display::OLED::sendMainImage();
									}
									view.setActiveModControllableTimelineCounter(newInstrument->activeClip);
								}
							}
							break;
						}
						}
					}
				}

				requestRendering(this, 1 << selectedClipYDisplay, 0);
			}
		}
	}
	else if (b == KIT) {
		newInstrumentType = InstrumentType::KIT;
		goto changeInstrumentType;
	}
	else if (b == MIDI) {
		newInstrumentType = InstrumentType::MIDI_OUT;
		goto changeInstrumentType;
	}
	else if (b == CV) {
		newInstrumentType = InstrumentType::CV;
		goto changeInstrumentType;
	}
	else if (b == KEYBOARD) {
		if (on && currentUIMode == UI_MODE_NONE) {
			changeRootUI(&performanceSessionView);
		}
	}
	else {
notDealtWith:
		return TimelineView::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
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

ActionResult SessionView::padAction(int32_t xDisplay, int32_t yDisplay, int32_t on) {
	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
		return gridHandlePads(xDisplay, yDisplay, on);
	}

	Clip* clip = getClipOnScreen(yDisplay);
	int32_t clipIndex = yDisplay + currentSong->songViewYScroll;

	// If we tapped on a Clip's main pads...
	if (xDisplay < kDisplayWidth) {

		// Press down
		if (on) {

			Buttons::recordButtonPressUsedUp = true;

			if (!Buttons::isShiftButtonPressed()) {

				if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING) {
					goto holdingRecord;
				}

				// If no Clip previously pressed...
				if (currentUIMode == UI_MODE_NONE) {

					// If they're holding down the record button...
					if (Buttons::isButtonPressed(deluge::hid::button::RECORD)) {

holdingRecord:
						// If doing recording stuff, create a "pending overdub".
						// We may or may not be doing a tempoless record and need to finish that up.
						if (playbackHandler.playbackState && currentPlaybackMode == &session) {

							Clip* sourceClip = getClipOnScreen(yDisplay + 1);

							if (!sourceClip) {
								return ActionResult::DEALT_WITH;
							}

							// If already has a pending overdub, get out
							if (currentSong->getPendingOverdubWithOutput(sourceClip->output)) {
								return ActionResult::DEALT_WITH;
							}

							if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
								display->displayPopup(
								    deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
								return ActionResult::DEALT_WITH;
							}

							if (sdRoutineLock) {
								return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
							}

							int32_t clipIndex = yDisplay + currentSong->songViewYScroll + 1;

							// If source clip currently recording, arm it to stop (but not if tempoless recording)
							if (playbackHandler.isEitherClockActive() && sourceClip->getCurrentlyRecordingLinearly()
							    && sourceClip->armState == ArmState::OFF) {
								session.toggleClipStatus(sourceClip, &clipIndex, false, kInternalButtonPressLatency);
							}

							OverDubType newOverdubNature =
							    (xDisplay < kDisplayWidth) ? OverDubType::Normal : OverDubType::ContinuousLayering;
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
									requestRendering(this, 0, 0xFFFFFFFF);
								}

								// If we were doing a tempoless record, now's the time to stop that and restart playback
								if (!playbackHandler.isEitherClockActive()) {
									playbackHandler.finishTempolessRecording(true, kInternalButtonPressLatency, false);
								}
							}
							else if (currentSong->anyClipsSoloing) {
								display->displayPopup(deluge::l10n::get(
								    deluge::l10n::String::STRING_FOR_CANT_CREATE_OVERDUB_WHILE_CLIPS_SOLOING));
							}
						}
					}

					// If Clip present here...
					else if (clip) {

						// If holding down tempo knob...
						if (Buttons::isButtonPressed(deluge::hid::button::TEMPO_ENC)) {
							playbackHandler.grabTempoFromClip(clip);
						}

						// If it's a pending overdub, delete it
						else if (clip->isPendingOverdub) {
removePendingOverdub:
							if (sdRoutineLock) {
								return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Possibly not quite necessary...
							}

							removeClip(getClipOnScreen(yDisplay));
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
							if (display->haveOLED()) {
								deluge::hid::display::OLED::sendMainImage();
							}
						}
					}

					// Otherwise, try and create one
					else {

						if (Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
							return ActionResult::DEALT_WITH;
						}
						if (sdRoutineLock) {
							return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
						}

						//if (possiblyCreatePendingNextOverdub(clipIndex, OVERDUB_EXTENDING)) return ActionResult::DEALT_WITH;

						clip = createNewInstrumentClip(yDisplay);
						if (!clip) {
							return ActionResult::DEALT_WITH;
						}

						int32_t numClips = currentSong->sessionClips.getNumElements();
						if (clipIndex < 0) {
							clipIndex = 0;
						}
						else if (clipIndex >= numClips) {
							clipIndex = numClips - 1;
						}

						selectedClipYDisplay = clipIndex - currentSong->songViewYScroll;
						requestRendering(this, 0, 1 << selectedClipYDisplay);
						goto startHoldingDown;
					}
				}

				// If Clip previously already pressed, clone it to newly-pressed row
				else if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
					if (selectedClipYDisplay != yDisplay && performActionOnPadRelease) {

						if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
							display->displayPopup(
							    deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
							return ActionResult::DEALT_WITH;
						}

						if (sdRoutineLock) {
							return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
						}

						actionLogger.deleteAllLogs();
						cloneClip(selectedClipYDisplay, yDisplay);
						goto justEndClipPress;
					}
				}

				else if (currentUIMode == UI_MODE_MIDI_LEARN) {
					if (clip) {

						// AudioClip
						if (clip->type == CLIP_TYPE_AUDIO) {
							if (sdRoutineLock) {
								return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
							}
							view.endMIDILearn();
							gui::context_menu::audioInputSelector.audioOutput = (AudioOutput*)clip->output;
							gui::context_menu::audioInputSelector.setupAndCheckAvailability();
							openUI(&gui::context_menu::audioInputSelector);
						}

						// InstrumentClip
						else {
midiLearnMelodicInstrumentAction:
							if (clip->output->type == InstrumentType::SYNTH
							    || clip->output->type == InstrumentType::MIDI_OUT
							    || clip->output->type == InstrumentType::CV) {

								if (sdRoutineLock) {
									return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
								}
								view.melodicInstrumentMidiLearnPadPressed(on, (MelodicInstrument*)clip->output);
							}
						}
					}
				}
			}
			else if (clip) {
				// Shift button is pressed, this is only interresting for changing colour
				selectedClipYDisplayForColorChange = yDisplay;
			}
		}

		// Release
		else {
			selectedClipYDisplayForColorChange = 255;

			// If Clip was pressed before...
			if (isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)) {

				// Stop stuttering if we are
				if (isUIModeActive(UI_MODE_STUTTERING)) {
					((ModControllableAudio*)view.activeModControllableModelStack.modControllable)
					    ->endStutter((ParamManagerForTimeline*)view.activeModControllableModelStack.paramManager);
				}

				if (performActionOnPadRelease && xDisplay == selectedClipPressXDisplay
				    && AudioEngine::audioSampleTimer - selectedClipTimePressed < kShortPressTime) {

					// Not allowed if recording arrangement
					if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
						display->displayPopup(
						    deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
						goto justEndClipPress;
					}

					if (sdRoutineLock) {
						return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}

					// Enter Clip
					Clip* clip = getClipOnScreen(selectedClipYDisplay);
					transitionToViewForClip(clip);
				}

				// If doing nothing, at least exit the submode - if this was that initial press
				else {
					if (yDisplay == selectedClipPressYDisplay && xDisplay == selectedClipPressXDisplay) {
justEndClipPress:
						if (sdRoutineLock) {
							return ActionResult::
							    REMIND_ME_OUTSIDE_CARD_ROUTINE; // If in card routine, might mean it's still loading an Instrument they selected,
						}
						// and we don't want the loading animation or anything to get stuck onscreen
						clipPressEnded();
					}
				}
			}

			else if (isUIModeActive(UI_MODE_MIDI_LEARN)) {
				if (clip && clip->type == CLIP_TYPE_INSTRUMENT) {
					requestRendering(this, 1 << yDisplay, 0);
					goto midiLearnMelodicInstrumentAction;
				}
			}

			// In all other cases, then if also inside card routine, do get it to remind us after. Especially important because it could be that
			// the user has actually pressed down on a pad, that's caused a new clip to be created and preset to load, which is still loading right now,
			// but the uiMode hasn't been set to "holding down" yet and control hasn't been released back to the user, and this is the user releasing their press,
			// so we definitely want to be reminded of this later after the above has happened.
			else {
				if (sdRoutineLock) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}
			}
		}
	}

	// Or, status or section (aka audition) pads
	else {

		if (playbackHandler.playbackState && currentPlaybackMode == &arrangement) {
			if (currentUIMode == UI_MODE_NONE) {
				if (sdRoutineLock) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}
				playbackHandler.switchToSession();
			}
		}

		else {

			if (clip && clip->isPendingOverdub) {
				if (on && !currentUIMode) {
					goto removePendingOverdub;
				}
			}

			// Status pad
			if (xDisplay == kDisplayWidth) {

				// If Clip is present here
				if (clip) {

					return view.clipStatusPadAction(clip, on, yDisplay);
				}
			}

			// Section pad
			else if (xDisplay == kDisplayWidth + 1) {

				if (on && Buttons::isButtonPressed(deluge::hid::button::RECORD)
				    && (!currentUIMode || currentUIMode == UI_MODE_VIEWING_RECORD_ARMING)) {
					Buttons::recordButtonPressUsedUp = true;
					goto holdingRecord;
				}

				// If Clip is present here
				if (clip) {

					switch (currentUIMode) {
					case UI_MODE_MIDI_LEARN:
						if (sdRoutineLock) {
							return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
						}
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

	return ActionResult::DEALT_WITH;
}

void SessionView::clipPressEnded() {
	// End stuttering since this can also end selection
	if (isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) && isUIModeActive(UI_MODE_STUTTERING)) {
		((ModControllableAudio*)view.activeModControllableModelStack.modControllable)
		    ->endStutter((ParamManagerForTimeline*)view.activeModControllableModelStack.paramManager);
	}

	if (isUIModeActive(UI_MODE_HOLDING_SECTION_PAD)) {
		exitUIMode(UI_MODE_HOLDING_SECTION_PAD);
		if (display->haveOLED()) {
			deluge::hid::display::OLED::removePopup();
		}
		else {
			redrawNumericDisplay();
		}
	}

	if (currentUIMode == UI_MODE_EXPLODE_ANIMATION) {
		return;
	}

	currentUIMode = UI_MODE_NONE;
	view.setActiveModControllableTimelineCounter(currentSong);
	if (display->haveOLED()) {
		renderUIsForOled();
		setCentralLEDStates();
	}
	else {
		redrawNumericDisplay();
	}
	selectedClipYDisplay = 255;
	gridResetPresses();
}

void SessionView::sectionPadAction(uint8_t y, bool on) {

	Clip* clip = getClipOnScreen(y);

	if (!clip) {
		return;
	}

	if (on) {

		if (isNoUIModeActive()) {
			// If user wanting to change Clip's section
			if (Buttons::isShiftButtonPressed()) {

				// Not allowed if recording arrangement
				if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
					return;
				}

				actionLogger.deleteAllLogs();

				uint8_t oldSection = clip->section;

				clip->section = 255;

				bool sectionUsed[kMaxNumSections];
				memset(sectionUsed, 0, sizeof(sectionUsed));

				for (int32_t c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
					Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);

					if (thisClip->section < kMaxNumSections) {
						sectionUsed[thisClip->section] = true;
					}
				}

				// Mark first unused section as available
				for (int32_t i = 0; i < kMaxNumSections; i++) {
					if (!sectionUsed[i]) {
						sectionUsed[i] = true;
						break;
					}
				}

				do {
					oldSection = (oldSection + 1) % kMaxNumSections;
				} while (!sectionUsed[oldSection]);

				clip->section = oldSection;

				requestRendering(this, 0, 1 << y);
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
				session.armSection(sectionPressed, kInternalButtonPressLatency);
			}
			exitUIMode(UI_MODE_HOLDING_SECTION_PAD);
			if (display->haveOLED()) {
				deluge::hid::display::OLED::removePopup();
			}
			else {
				redrawNumericDisplay();
			}
			uiTimerManager.unsetTimer(TIMER_UI_SPECIFIC);
		}

		else if (isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)) {
			session.armSection(clip->section, kInternalButtonPressLatency);
		}
	}
}

ActionResult SessionView::timerCallback() {
	switch (currentUIMode) {

	case UI_MODE_HOLDING_SECTION_PAD:
		beginEditingSectionRepeatsNum();
		break;

	case UI_MODE_NONE:
		if (Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
			if (currentSong->sessionLayout != SessionLayoutType::SessionLayoutTypeGrid
			    || (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid
			        && gridModeActive == SessionGridModeLaunch)) {
				enterUIMode(UI_MODE_VIEWING_RECORD_ARMING);
				PadLEDs::reassessGreyout(false);
			}
		case UI_MODE_VIEWING_RECORD_ARMING:
			requestRendering(this, 0, 0xFFFFFFFF);
			view.blinkOn = !view.blinkOn;
			uiTimerManager.setTimer(TIMER_UI_SPECIFIC, kFastFlashTime);
		}
		break;
	}

	return ActionResult::DEALT_WITH;
}

void SessionView::drawSectionRepeatNumber() {
	int32_t number = currentSong->sections[sectionPressed].numRepetitions;
	char const* outputText;
	if (display->haveOLED()) {
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

		if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
			display->popupText(outputText);
		}
		else {
			display->popupTextTemporary(outputText);
		}
	}
	else {
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
		display->setText(outputText, true, 255, true);
	}
}

void SessionView::selectEncoderAction(int8_t offset) {
	if (currentUIMode == UI_MODE_HOLDING_SECTION_PAD) {
		if (performActionOnSectionPadRelease) {
			beginEditingSectionRepeatsNum();
		}
		else {
			int16_t* numRepetitions = &currentSong->sections[sectionPressed].numRepetitions;
			*numRepetitions += offset;
			if (*numRepetitions > 9999) {
				*numRepetitions = 9999;
			}
			else if (*numRepetitions < -1) {
				*numRepetitions = -1;
			}
			drawSectionRepeatNumber();
		}
	}
	else if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
		performActionOnPadRelease = false;

		if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
			return;
		}

		Clip* clip = getClipForLayout();

		if (clip == nullptr) {
			return;
		}

		if (clip->type == CLIP_TYPE_INSTRUMENT) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack =
			    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);

			switch (currentSong->sessionLayout) {
			case SessionLayoutType::SessionLayoutTypeRows: {
				view.navigateThroughPresetsForInstrumentClip(offset, modelStack, true);
				break;
			}
			case SessionLayoutType::SessionLayoutTypeGrid: {
				Output* oldOutput = clip->output;
				Output* newOutput = currentSong->navigateThroughPresetsForInstrument(oldOutput, offset);
				if (oldOutput != newOutput) {
					view.setActiveModControllableTimelineCounter(newOutput->activeClip);
					requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
				}
				break;
			}
			}
		}
		else {
			// This moves clips around uncomfortably and we have a track for every Audio anyway
			if (currentSong->sessionLayout != SessionLayoutType::SessionLayoutTypeGrid) {
				view.navigateThroughAudioOutputsForAudioClip(offset, (AudioClip*)clip, true);
			}
		}
	}
	else if (currentUIMode == UI_MODE_NONE && sessionButtonActive) {
		sessionButtonUsed = true;
		selectLayout(offset);
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

void SessionView::editNumRepeatsTilLaunch(int32_t offset) {
	session.numRepeatsTilLaunch += offset;
	if (session.numRepeatsTilLaunch < 1) {
		session.numRepeatsTilLaunch = 1;
	}
	else if (session.numRepeatsTilLaunch > 9999) {
		session.numRepeatsTilLaunch = 9999;
	}
	else {
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			redrawNumericDisplay();
		}
	}
}

ActionResult SessionView::horizontalEncoderAction(int32_t offset) {
	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
		return gridHandleScroll(offset, 0);
	}

	// So long as we're not in a submode...
	if (isNoUIModeActive()) {

		// Or, if the shift key is pressed
		if (Buttons::isShiftButtonPressed()) {
			// Tell the user why they can't resize
			indicator_leds::indicateAlertOnLed(IndicatorLED::CLIP_VIEW);
			return ActionResult::DEALT_WITH;
		}
	}

	return ClipNavigationTimelineView::horizontalEncoderAction(offset);
}

ActionResult SessionView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {

	if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW
	    || currentUIMode == UI_MODE_VIEWING_RECORD_ARMING) {

		if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.
		}

		// Change row color by pressing row & shift - same shortcut as in clip view.
		if ((currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW || selectedClipYDisplayForColorChange != 255)
		    && Buttons::isShiftButtonPressed()) {
			auto yDisplay =
			    (selectedClipYDisplayForColorChange != 255) ? selectedClipYDisplayForColorChange : selectedClipYDisplay;

			Clip* clip = getClipOnScreen(yDisplay);
			if (!clip)
				return ActionResult::NOT_DEALT_WITH;

			clip->colourOffset += offset;
			requestRendering(this, 1 << yDisplay, 0);

			return ActionResult::DEALT_WITH;
		}

		if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
			// For safety, is used in verticalScrollOneSquare on clip copy
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			return gridHandleScroll(0, offset);
		}

		return verticalScrollOneSquare(offset);
	}

	return ActionResult::DEALT_WITH;
}

ActionResult SessionView::verticalScrollOneSquare(int32_t direction) {

	if (direction == 1) {
		if (currentSong->songViewYScroll >= currentSong->sessionClips.getNumElements() - 1) {
			return ActionResult::DEALT_WITH;
		}
	}
	else {
		if (currentSong->songViewYScroll <= 1 - kDisplayHeight) {
			return ActionResult::DEALT_WITH;
		}
	}

	// Drag Clip along with scroll if one is selected
	if (isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)) {

		performActionOnPadRelease = false;

		// Not allowed if recording arrangement
		if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
			return ActionResult::DEALT_WITH;
		}

		int32_t oldIndex = selectedClipYDisplay + currentSong->songViewYScroll;

		if (direction == 1) {
			if (oldIndex >= currentSong->sessionClips.getNumElements() - 1) {
				return ActionResult::DEALT_WITH;
			}
		}
		else {
			if (oldIndex <= 0) {
				return ActionResult::DEALT_WITH;
			}
		}

		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		actionLogger.deleteAllLogs();

		int32_t newIndex = oldIndex + direction;
		currentSong->sessionClips.swapElements(newIndex, oldIndex);
	}

	currentSong->songViewYScroll += direction;
	redrawClipsOnScreen();

	if (isUIModeActive(UI_MODE_VIEWING_RECORD_ARMING)) {
		PadLEDs::reassessGreyout(true);
	}

	return ActionResult::DEALT_WITH;
}

bool SessionView::renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
		return gridRenderSidebar(whichRows, image, occupancyMask);
	}

	for (int32_t i = 0; i < kDisplayHeight; i++) {
		if (whichRows & (1 << i)) {
			drawStatusSquare(i, image[i]);
			drawSectionSquare(i, image[i]);
		}
	}

	return true;
}

void SessionView::drawStatusSquare(uint8_t yDisplay, uint8_t thisImage[][3]) {
	uint8_t* thisColour = thisImage[kDisplayWidth];

	Clip* clip = getClipOnScreen(yDisplay);

	// If no Clip, black
	if (!clip) {
		memset(thisColour, 0, 3);
	}
	else {
		view.getClipMuteSquareColour(clip, thisColour);
	}
}

void SessionView::drawSectionSquare(uint8_t yDisplay, uint8_t thisImage[][3]) {
	uint8_t* thisColour = thisImage[kDisplayWidth + 1];

	Clip* clip = getClipOnScreen(yDisplay);

	// If no Clip, black
	if (!clip) {
		memset(thisColour, 0, 3);
	}
	else {
		if (view.midiLearnFlashOn && currentSong->sections[clip->section].launchMIDICommand.containsSomething()) {
			thisColour[0] = midiCommandColour.r;
			thisColour[1] = midiCommandColour.g;
			thisColour[2] = midiCommandColour.b;
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
int32_t setPresetOrNextUnlaunchedOne(InstrumentClip* clip, InstrumentType instrumentType, bool* instrumentAlreadyInSong,
                                     bool copyDrumsFromClip = true) {
	ReturnOfConfirmPresetOrNextUnlaunchedOne result;
	result.error = Browser::currentDir.set(getInstrumentFolder(instrumentType));
	if (result.error) {
		return result.error;
	}

	result = loadInstrumentPresetUI.findAnUnlaunchedPresetIncludingWithinSubfolders(currentSong, instrumentType,
	                                                                                Availability::INSTRUMENT_UNUSED);
	if (result.error) {
		return result.error;
	}

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

	if (result.error) {
		return result.error;
	}

	if (isHibernating) {
		currentSong->removeInstrumentFromHibernationList(newInstrument);
	}

	if (display->haveOLED()) {
		deluge::hid::display::OLED::displayWorkingAnimation("Loading");
	}
	else {
		display->displayLoadingAnimation();
	}

	newInstrument->loadAllAudioFiles(true);

	display->removeWorkingAnimation();

	if (copyDrumsFromClip) {
		result.error = clip->setAudioInstrument(newInstrument, currentSong, true, NULL); // Does a setupPatching()
		if (result.error) {
			// TODO: needs more thought - we'd want to deallocate the Instrument...
			return result.error;
		}

		if (instrumentType == InstrumentType::KIT) {

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack =
			    setupModelStackWithSong(modelStackMemory, currentSong)->addTimelineCounter(clip);

			clip->assignDrumsToNoteRows(modelStack); // Does a setupPatching() for each Drum
			clip->yScroll = 0;
		}
	}
	else {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithSong(modelStackMemory, currentSong)->addTimelineCounter(clip);
		int32_t error = clip->changeInstrument(modelStack, newInstrument, NULL, InstrumentRemoval::NONE);
		if (error != NO_ERROR) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_SWITCHING_TO_TRACK_FAILED));
		}

		if (newInstrument->type == InstrumentType::KIT) {
			clip->yScroll = 0;
		}
	}

	return NO_ERROR;
}

constexpr float colourStep = 22.5882352941;
static float lastColour = 192 - colourStep + 1;

Clip* SessionView::createNewInstrumentClip(int32_t yDisplay) {
	actionLogger.deleteAllLogs();

	void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(InstrumentClip));
	if (memory == nullptr) {
		display->displayError(ERROR_INSUFFICIENT_RAM);
		return nullptr;
	}

	InstrumentClip* newClip = new (memory) InstrumentClip(currentSong);

	uint32_t currentDisplayLength = currentSong->xZoom[NAVIGATION_CLIP] * kDisplayWidth;

	if (playbackHandler.playbackState
	    && (currentPlaybackMode == &arrangement || !playbackHandler.isEitherClockActive())) {
		newClip->activeIfNoSolo = false;
	}

	uint32_t oneBar = currentSong->getBarLength();

	// Default Clip length. Default to current zoom, minimum 1 bar
	int32_t newClipLength = std::max(currentDisplayLength, oneBar);

	newClip->colourOffset = random(72);
	newClip->loopLength = newClipLength;

	bool instrumentAlreadyInSong;

	InstrumentType instrumentType = InstrumentType::SYNTH;
doGetInstrument:
	int32_t error = setPresetOrNextUnlaunchedOne(newClip, instrumentType, &instrumentAlreadyInSong);
	if (error) {

		// If that was for a synth and there were none, try a kit
		if (error == ERROR_NO_FURTHER_PRESETS && instrumentType == InstrumentType::SYNTH) {
			instrumentType = InstrumentType::KIT;
			goto doGetInstrument;
		}
		newClip->~InstrumentClip();
		delugeDealloc(memory);
		display->displayError(error);
		return NULL;
	}

	int32_t index = yDisplay + currentSong->songViewYScroll;
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

	if (!instrumentAlreadyInSong) {
		currentSong->addOutput(newClip->output);
	}

	// Possibly want to set this as the active Clip...
	if (!newClip->output->activeClip) {
		newClip->output->setActiveClip(modelStackWithTimelineCounter);
	}

	return newClip;
}

void SessionView::replaceAudioClipWithInstrumentClip(Clip* clip, InstrumentType instrumentType) {
	int32_t clipIndex = currentSong->sessionClips.getIndexForClip(clip);

	if (!clip || clip->type != CLIP_TYPE_AUDIO) {
		return;
	}

	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid
	    && currentSong->getClipWithOutput(clip->output, false, clip)) {
		display->displayPopup(deluge::l10n::get(
		    deluge::l10n::String::STRING_FOR_AUDIO_TRACKS_WITH_CLIPS_CANT_BE_TURNED_INTO_AN_INSTRUMENT));
		return;
	}

	AudioClip* audioClip = (AudioClip*)clip;
	if (audioClip->sampleHolder.audioFile || audioClip->getCurrentlyRecordingLinearly()) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIP_NOT_EMPTY));
		return;
	}

	// Allocate memory for InstrumentClip
	void* clipMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(InstrumentClip));
	if (!clipMemory) {
ramError:
		display->displayError(ERROR_INSUFFICIENT_RAM);
		return;
	}

	// Create the audio clip and ParamManager
	InstrumentClip* newClip = new (clipMemory) InstrumentClip(currentSong);

	// Give the new clip its stuff
	newClip->cloneFrom(clip);
	newClip->colourOffset = random(72);

	bool instrumentAlreadyInSong;
	int32_t error;

	if (instrumentType == InstrumentType::SYNTH || instrumentType == InstrumentType::KIT) {

		error = setPresetOrNextUnlaunchedOne(newClip, instrumentType, &instrumentAlreadyInSong);
		if (error) {
gotError:
			display->displayError(error);
gotErrorDontDisplay:
			newClip->~InstrumentClip();
			delugeDealloc(clipMemory);
			return;
		}
	}

	else {
		Instrument* newInstrument = currentSong->getNonAudioInstrumentToSwitchTo(
		    instrumentType, Availability::INSTRUMENT_UNUSED, 0, -1, &instrumentAlreadyInSong);
		if (!newInstrument) {
			goto gotErrorDontDisplay;
		}

		error = newClip->setNonAudioInstrument(newInstrument, currentSong);
		if (error) {
			// TODO: we'd really want to deallocate the Instrument
			goto gotError;
		}
	}

	if (!instrumentAlreadyInSong) {
		currentSong->addOutput(newClip->output);
	}

	// Possibly want to set this as the active Clip...
	if (!newClip->output->activeClip) {

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(newClip);

		newClip->output->setActiveClip(modelStackWithTimelineCounter);
	}

	newClip->output->colour = clip->output->colour;

	currentSong->swapClips(newClip, clip, clipIndex);

	view.setActiveModControllableTimelineCounter(newClip);
	view.displayOutputName(newClip->output, true, newClip);

	if (display->haveOLED()) {
		deluge::hid::display::OLED::sendMainImage();
	}
}

void SessionView::replaceInstrumentClipWithAudioClip(Clip* clip) {
	int32_t clipIndex = currentSong->sessionClips.getIndexForClip(clip);

	if (!clip || clip->type != CLIP_TYPE_INSTRUMENT) {
		return;
	}

	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid
	    && currentSong->getClipWithOutput(clip->output, false, clip)) {
		display->displayPopup(deluge::l10n::get(
		    deluge::l10n::String::STRING_FOR_INSTRUMENTS_WITH_CLIPS_CANT_BE_TURNED_INTO_AUDIO_TRACKS));
		return;
	}

	InstrumentClip* instrumentClip = (InstrumentClip*)clip;
	if (instrumentClip->containsAnyNotes() || instrumentClip->output->clipHasInstance(clip)) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIP_NOT_EMPTY));
		return;
	}

	Clip* newClip = currentSong->replaceInstrumentClipWithAudioClip(clip, clipIndex);

	if (!newClip) {
		display->displayError(ERROR_INSUFFICIENT_RAM);
		return;
	}

	currentSong->arrangementYScroll--; // Is our best bet to avoid the scroll appearing to change visually

	view.setActiveModControllableTimelineCounter(newClip);
	view.displayOutputName(newClip->output, true, newClip);

	if (display->haveOLED()) {
		deluge::hid::display::OLED::sendMainImage();
	}
	// If Clip was in keyboard view, need to redraw that
	requestRendering(this, 1 << selectedClipYDisplay, 1 << selectedClipYDisplay);
}

void SessionView::removeClip(Clip* clip) {
	currentSong->ensureAllInstrumentsHaveAClipOrBackedUpParamManager(
	    "E373", "H373"); // Trying to narrow down H067 that Leo got, below.

	if (!clip) {
		return;
	}

	int32_t clipIndex = currentSong->sessionClips.getIndexForClip(clip);

	// If last session Clip left, just don't allow. Easiest
	if (currentSong->sessionClips.getNumElements() == 1) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_REMOVE_FINAL_CLIP));
		return;
	}

	// If this Clip is the inputTickScaleClip
	if (clip == currentSong->getSyncScalingClip()) {
		// Don't let the user do it
		indicator_leds::indicateAlertOnLed(IndicatorLED::SYNC_SCALING);
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

Clip* SessionView::getClipOnScreen(int32_t yDisplay) {
	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
		if (gridFirstPadActive()) {
			return gridClipFromCoords(gridFirstPressedX, gridFirstPressedY);
		}

		return nullptr;
	}

	int32_t index = yDisplay + currentSong->songViewYScroll;

	if (index < 0 || index >= currentSong->sessionClips.getNumElements()) {
		return NULL;
	}

	return currentSong->sessionClips.getClipAtIndex(index);
}

void SessionView::redrawClipsOnScreen(bool doRender) {
	if (doRender) {
		requestRendering(this);
	}
	view.flashPlayEnable();
}

void SessionView::setLedStates() {

	indicator_leds::setLedState(IndicatorLED::KEYBOARD, false);

	view.setLedStates();

#ifdef currentClipStatusButtonX
	view.switchOffCurrentClipPad();
#endif
}

extern char loopsRemainingText[];

void SessionView::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	renderViewDisplay(getCurrentUI() == &arrangerView ? l10n::get(l10n::String::STRING_FOR_ARRANGER_VIEW)
	                                                  : l10n::get(l10n::String::STRING_FOR_SONG_VIEW));

	if (playbackHandler.isEitherClockActive()) {
		// Session playback
		if (currentPlaybackMode == &session) {
			if (session.launchEventAtSwungTickCount) {
yesDoIt:
				intToString(session.numRepeatsTilLaunch, &loopsRemainingText[17]);
				deluge::hid::display::OLED::drawPermanentPopupLookingText(loopsRemainingText);
			}
		}

		else { // Arrangement playback
			if (playbackHandler.stopOutputRecordingAtLoopEnd) {
				deluge::hid::display::OLED::drawPermanentPopupLookingText("Resampling will end...");
			}
		}
	}
}

void SessionView::redrawNumericDisplay() {
	renderViewDisplay(getCurrentUI() == &arrangerView ? l10n::get(l10n::String::STRING_FOR_ARRANGER_VIEW)
	                                                  : l10n::get(l10n::String::STRING_FOR_SONG_VIEW));

	if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
		return;
	}

	// If playback on...
	if (playbackHandler.isEitherClockActive()) {

		// Session playback
		if (currentPlaybackMode == &session) {
			if (!session.launchEventAtSwungTickCount) {
				goto nothingToDisplay;
			}

			if (getCurrentUI() == &loadSongUI) {
				if (currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
yesDoIt:
					char buffer[5];
					intToString(session.numRepeatsTilLaunch, buffer);
					display->setText(buffer, true, 255, true, NULL, false, true);
				}
			}

			else if (getCurrentUI() == &arrangerView) {
				if (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_HOLDING_ARRANGEMENT_ROW
				    || currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
					if (session.switchToArrangementAtLaunchEvent) {
						goto yesDoIt;
					}
					else {
						goto setBlank;
					}
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
						display->setText("1", true, 255, true, NULL, false, true);
					}
					else {
						goto setBlank;
					}
				}
			}
			else if (getCurrentUI() == this) {
setBlank:
				display->setText("");
			}
		}
	}

	// Or if no playback active...
	else {
nothingToDisplay:
		if (getCurrentUI() == this || getCurrentUI() == &arrangerView) {
			if (currentUIMode != UI_MODE_HOLDING_SECTION_PAD) {
				display->setText("");
			}
		}
	}

	setCentralLEDStates();
}

//render session view display on opening
void SessionView::renderViewDisplay(char const* viewString) {
	if (display->haveOLED()) {
		deluge::hid::display::OLED::clearMainImage();

#if OLED_MAIN_HEIGHT_PIXELS == 64
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif

		yPos = yPos + 12;

		deluge::hid::display::OLED::drawStringCentred(viewString, yPos, deluge::hid::display::OLED::oledMainImage[0],
		                                              OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSpacingY);

		deluge::hid::display::OLED::sendMainImage();
	}
	else {
		display->setScrollingText(viewString);
	}
}

// This gets called by redrawNumericDisplay() - or, if OLED, it gets called instead, because this still needs to happen.
void SessionView::setCentralLEDStates() {
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
	indicator_leds::setLedState(IndicatorLED::KEYBOARD, false);

	if (getCurrentUI() == this) {
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	}
}

uint32_t SessionView::getMaxZoom() {
	return currentSong->getLongestClip(true, false)->getMaxZoom();
}

void SessionView::cloneClip(uint8_t yDisplayFrom, uint8_t yDisplayTo) {
	Clip* clipToClone = getClipOnScreen(yDisplayFrom);
	if (!clipToClone) {
		return;
	}

	// Just don't allow cloning of Clips which are linearly recording
	if (clipToClone->getCurrentlyRecordingLinearly()) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_IN_PROGRESS));
		return;
	}

	bool enoughSpace = currentSong->sessionClips.ensureEnoughSpaceAllocated(1);
	if (!enoughSpace) {
ramError:
		display->displayError(ERROR_INSUFFICIENT_RAM);
		return;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithSong(modelStackMemory, currentSong)->addTimelineCounter(clipToClone);

	int32_t error = clipToClone->clone(modelStack);
	if (error) {
		goto ramError;
	}

	Clip* newClip = (Clip*)modelStack->getTimelineCounter();

	newClip->section = (uint8_t)(newClip->section + 1) % kMaxNumSections;

	int32_t newIndex = yDisplayTo + currentSong->songViewYScroll;

	if (yDisplayTo < yDisplayFrom) {
		currentSong->songViewYScroll++;
		newIndex++;
	}

	if (newIndex < 0) {
		newIndex = 0;
	}
	else if (newIndex > currentSong->sessionClips.getNumElements()) {
		newIndex = currentSong->sessionClips.getNumElements();
	}

	currentSong->sessionClips.insertClipAtIndex(newClip, newIndex); // Can't fail - we ensured enough space in advance

	redrawClipsOnScreen();
}

void SessionView::graphicsRoutine() {
	static int counter = 0;
	if (currentUIMode == UI_MODE_NONE) {
		int32_t modKnobMode = -1;
		bool editingComp = false;
		if (view.activeModControllableModelStack.modControllable) {
			uint8_t* modKnobModePointer = view.activeModControllableModelStack.modControllable->getModKnobMode();
			if (modKnobModePointer) {
				modKnobMode = *modKnobModePointer;
				editingComp = view.activeModControllableModelStack.modControllable->isEditingComp();
			}
		}
		if (modKnobMode == 4 && editingComp) { //upper
			counter = (counter + 1) % 5;
			if (counter == 0) {
				uint8_t gr = AudioEngine::mastercompressor.gainReduction;

				indicator_leds::setMeterLevel(1, gr); //Gain Reduction LED
			}
		}
	}

	uint8_t tickSquares[kDisplayHeight];
	uint8_t colours[kDisplayHeight];

	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
		// Nothing to do here but clear since we don't render playhead
		memset(&tickSquares, 255, sizeof(tickSquares));
		memset(&colours, 255, sizeof(colours));
		PadLEDs::setTickSquares(tickSquares, colours);
		return;
	}

	bool anyLinearRecordingOnThisScreen = false;
	bool anyLinearRecordingOnNextScreen = false;

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		int32_t newTickSquare;

		Clip* clip = getClipOnScreen(yDisplay);

		if (!playbackHandler.playbackState || !clip || !currentSong->isClipActive(clip)
		    || playbackHandler.ticksLeftInCountIn || currentUIMode == UI_MODE_HORIZONTAL_ZOOM
		    || (currentUIMode == UI_MODE_HORIZONTAL_SCROLL && PadLEDs::transitionTakingPlaceOnRow[yDisplay])) {
			newTickSquare = 255;
		}

		// Tempoless recording
		else if (!playbackHandler.isEitherClockActive()) {
			newTickSquare = kDisplayWidth - 1;

			if (clip->getCurrentlyRecordingLinearly()) { // This would have to be true if we got here, I think?
				if (clip->type == CLIP_TYPE_AUDIO) {
					((AudioClip*)clip)->renderData.xScroll = -1; // Make sure values are recalculated

					rowNeedsRenderingDependingOnSubMode(yDisplay);
				}
				colours[yDisplay] = 2;
			}
		}
		else {
			int32_t localScroll =
			    getClipLocalScroll(clip, currentSong->xScroll[NAVIGATION_CLIP], currentSong->xZoom[NAVIGATION_CLIP]);
			Clip* clipToRecordTo = clip->getClipToRecordTo();
			int32_t livePos = clipToRecordTo->getLivePos();

			// If we are recording to another Clip, we have to use its position.
			if (clipToRecordTo != clip) {
				int32_t whichRepeat = (uint32_t)livePos / (uint32_t)clip->loopLength;
				livePos -= whichRepeat * clip->loopLength;

				// But if it's currently reversing, we have to re-apply that here.
				if (clip->sequenceDirectionMode == SequenceDirection::REVERSE
				    || (clip->sequenceDirectionMode == SequenceDirection::PINGPONG && (whichRepeat & 1))) {
					livePos = -livePos;
					if (livePos < 0) {
						livePos += clip->loopLength;
					}
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
				    && (clip->armState == ArmState::OFF
				        || xScrollBeforeFollowingAutoExtendingLinearRecording
				               != -1)) { // Only if it's auto extending, or it was before
					if (newTickSquare < kDisplayWidth) {
						anyLinearRecordingOnThisScreen = true;
					}
					else if (newTickSquare == kDisplayWidth) {
						anyLinearRecordingOnNextScreen = true;
					}
				}

				colours[yDisplay] = 2;
			}

			// Not linearly recording
			else {
				colours[yDisplay] = 0;
			}

			if (newTickSquare < 0 || newTickSquare >= kDisplayWidth) {
				newTickSquare = 255;
			}
		}

		tickSquares[yDisplay] = newTickSquare;
	}

	// Auto scrolling for linear recording --------

	// If no linear recording onscreen now...
	if (!anyLinearRecordingOnThisScreen && currentUIMode != UI_MODE_HORIZONTAL_SCROLL) {

		// If there's some on the next screen to the right, go there
		if (anyLinearRecordingOnNextScreen) {

			if (!currentUIMode && getCurrentUI() == this) {

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
	}

	PadLEDs::setTickSquares(tickSquares, colours);
}

void SessionView::requestRendering(UI* ui, uint32_t whichMainRows, uint32_t whichSideRows) {
	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
		// Just redrawing should be faster than evaluating every cell in every row
		uiNeedsRendering(ui, 0xFFFFFFFF, 0xFFFFFFFF);
	}

	uiNeedsRendering(ui, whichMainRows, whichSideRows);
}

void SessionView::rowNeedsRenderingDependingOnSubMode(int32_t yDisplay) {

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
		requestRendering(this, 1 << yDisplay, 0);
	}
}

bool SessionView::calculateZoomPinSquares(uint32_t oldScroll, uint32_t newScroll, uint32_t newZoom, uint32_t oldZoom) {

	bool anyToDo = false;

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

		Clip* clip = getClipOnScreen(yDisplay);

		if (clip && clip->currentlyScrollableAndZoomable()) {
			int32_t oldLocal = getClipLocalScroll(clip, oldScroll, oldZoom);
			int32_t newLocal = getClipLocalScroll(clip, newScroll, newZoom);

			PadLEDs::zoomPinSquare[yDisplay] =
			    ((int64_t)(int32_t)(oldLocal - newLocal) << 16) / (int32_t)(newZoom - oldZoom);
			PadLEDs::transitionTakingPlaceOnRow[yDisplay] = true;
			anyToDo = true;
		}
		else {
			PadLEDs::transitionTakingPlaceOnRow[yDisplay] = false;
		}
	}

	return anyToDo;
}

int32_t SessionView::getClipPlaceOnScreen(Clip* clip) {
	return currentSong->sessionClips.getIndexForClip(clip) - currentSong->songViewYScroll;
}

uint32_t SessionView::getMaxLength() {
	return currentSong->getLongestClip(true, false)->loopLength;
}

bool SessionView::setupScroll(uint32_t oldScroll) {
	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
		return false;
	}
	// Ok I'm sorta pretending that this is definitely previously false, though only one caller of this function actually
	// checks for that. Should be ok-ish though...
	pendingUIRenderingLock = true;

	uint32_t xZoom = currentSong->xZoom[NAVIGATION_CLIP];

	bool anyMoved = false;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {

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
	return std::min((clip->loopLength - 1) / (xZoom * kDisplayWidth) * xZoom * kDisplayWidth, overviewScroll);
}

void SessionView::flashPlayRoutine() {
	view.clipArmFlashOn = !view.clipArmFlashOn;

	switch (currentSong->sessionLayout) {
	case SessionLayoutType::SessionLayoutTypeRows: {
		uint32_t whichRowsNeedReRendering = 0;
		bool any = false;
		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			Clip* clip = getClipOnScreen(yDisplay);
			if ((clip != nullptr) && clip->armState != ArmState::OFF) {
				whichRowsNeedReRendering |= (1 << yDisplay);
			}
		}

		if (whichRowsNeedReRendering) {
			view.flashPlayEnable();
			requestRendering(this, 0, whichRowsNeedReRendering);
		}
		break;
	}
	case SessionLayoutType::SessionLayoutTypeGrid: {
		bool renderFlashing = false;
		for (int32_t idxClip = 0; idxClip < currentSong->sessionClips.getNumElements(); ++idxClip) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
			if (clip->armState != ArmState::OFF) {
				renderFlashing = true;
				break;
			}
		}

		// view.clipArmFlashOn needs to be off so the pad is finally rendered after flashing
		if (renderFlashing || view.clipArmFlashOn) {
			if (currentUIMode != UI_MODE_EXPLODE_ANIMATION) {
				requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
				view.flashPlayEnable();
			}
		}
		break;
	}
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

	if (currentUIMode == UI_MODE_HORIZONTAL_SCROLL) {
		return; // Is this 100% correct? What if that one Clip isn't visually scrolling?
	}

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		Clip* clip = getClipOnScreen(yDisplay);
		if (clip == instrumentClip) {
			requestRendering(this, 1 << yDisplay, 0);
			return;
		}
	}
}

uint32_t SessionView::getGreyedOutRowsNotRepresentingOutput(Output* output) {
	uint32_t rows = 0xFFFFFFFF;
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		Clip* clip = getClipOnScreen(yDisplay);
		if (clip && clip->output == output) {
			rows &= ~(1 << yDisplay);
		}
	}
	return rows;
}

bool SessionView::renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                 uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
		return gridRenderMainPads(whichRows, image, occupancyMask, drawUndefinedArea);
	}

	uint32_t whichRowsCouldntBeRendered = 0;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	PadLEDs::renderingLock = true;

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (whichRows & (1 << yDisplay)) {
			bool success = renderRow(modelStack, yDisplay, image[yDisplay], occupancyMask[yDisplay], drawUndefinedArea);
			if (!success) {
				whichRowsCouldntBeRendered |= (1 << yDisplay);
			}
		}
	}
	PadLEDs::renderingLock = false;

	if (whichRowsCouldntBeRendered && image == PadLEDs::image) {
		requestRendering(this, whichRowsCouldntBeRendered, 0);
	}

	return true;
}

// Returns false if can't because in card routine
bool SessionView::renderRow(ModelStack* modelStack, uint8_t yDisplay,
                            uint8_t thisImage[kDisplayWidth + kSideBarWidth][3],
                            uint8_t thisOccupancyMask[kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {

	Clip* clip = getClipOnScreen(yDisplay);

	if (clip) {

		// If user assigning MIDI controls and this Clip has a command assigned, flash pink
		if (view.midiLearnFlashOn
		    && (clip->output->type == InstrumentType::SYNTH || clip->output->type == InstrumentType::MIDI_OUT
		        || clip->output->type == InstrumentType::CV)
		    && ((MelodicInstrument*)clip->output)->midiInput.containsSomething()) {

			for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
				// We halve the intensity of the brightness in this case, because a lot of pads will be lit, it looks mental, and I think one user was having it
				// cause his Deluge to freeze due to underpowering.
				thisImage[xDisplay][0] = midiCommandColour.r >> 1;
				thisImage[xDisplay][1] = midiCommandColour.g >> 1;
				thisImage[xDisplay][2] = midiCommandColour.b >> 1;
			}
		}

		else {

			bool success = true;

			if (clip->isPendingOverdub) {
				for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
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

			if (view.thingPressedForMidiLearn == MidiLearn::MELODIC_INSTRUMENT_INPUT && view.midiLearnFlashOn
			    && view.learnedThing
			           == &((MelodicInstrument*)clip->output)
			                   ->midiInput) { // Should be fine even if output isn't a MelodicInstrument

				for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
					thisImage[xDisplay][0] >>= 1;
					thisImage[xDisplay][1] >>= 1;
					thisImage[xDisplay][2] >>= 1;
				}
			}

			return success;
		}
	}
	else {
		memset(thisImage, 0, kDisplayWidth * 3);
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

	int32_t clipPlaceOnScreen = std::clamp(getClipPlaceOnScreen(clip), -1_i32, kDisplayHeight);

	currentSong->xScroll[NAVIGATION_CLIP] =
	    getClipLocalScroll(clip, currentSong->xScroll[NAVIGATION_CLIP], currentSong->xZoom[NAVIGATION_CLIP]);

	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
		gridTransitionToViewForClip(clip);
		return;
	}

	PadLEDs::recordTransitionBegin(kClipCollapseSpeed);

	// InstrumentClips
	if (clip->type == CLIP_TYPE_INSTRUMENT) {

		currentUIMode = UI_MODE_INSTRUMENT_CLIP_EXPANDING;

		if (((InstrumentClip*)clip)->onKeyboardScreen) {

			keyboardScreen.renderMainPads(0xFFFFFFFF, PadLEDs::imageStore, PadLEDs::occupancyMaskStore);

			PadLEDs::numAnimatedRows = kDisplayHeight;
			for (int32_t y = 0; y < PadLEDs::numAnimatedRows; y++) {
				PadLEDs::animatedRowGoingTo[y] = clipPlaceOnScreen;
				PadLEDs::animatedRowGoingFrom[y] = y;
			}
		}

		else if (((InstrumentClip*)clip)->onAutomationInstrumentClipView) {

			// Won't have happened automatically because we haven't begun the "session"
			instrumentClipView.recalculateColours();

			automationInstrumentClipView.renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[1],
			                                            &PadLEDs::occupancyMaskStore[1], false);
			instrumentClipView.renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1]);

			// Important that this is done after currentSong->xScroll is changed, above
			instrumentClipView.fillOffScreenImageStores();

			PadLEDs::numAnimatedRows = kDisplayHeight + 2;
			for (int32_t y = 0; y < PadLEDs::numAnimatedRows; y++) {
				PadLEDs::animatedRowGoingTo[y] = clipPlaceOnScreen;
				PadLEDs::animatedRowGoingFrom[y] = y - 1;
			}
		}

		else {

			// Won't have happened automatically because we haven't begun the "session"
			instrumentClipView.recalculateColours();

			instrumentClipView.renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1],
			                                  false);
			instrumentClipView.renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1]);

			// Important that this is done after currentSong->xScroll is changed, above
			instrumentClipView.fillOffScreenImageStores();

			PadLEDs::numAnimatedRows = kDisplayHeight + 2;
			for (int32_t y = 0; y < PadLEDs::numAnimatedRows; y++) {
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

void SessionView::transitionToSessionView() {
	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
		gridTransitionToSessionView();
		return;
	}

	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {
		AudioClip* clip = (AudioClip*)currentSong->currentClip;
		if (!clip || !clip->sampleHolder.audioFile) { // !clip probably couldn't happen, but just in case...
			memcpy(PadLEDs::imageStore, PadLEDs::image, sizeof(PadLEDs::image));
			finishedTransitioningHere();
		}
		else {
			currentUIMode = UI_MODE_AUDIO_CLIP_COLLAPSING;
			waveformRenderer.collapseAnimationToWhichRow = getClipPlaceOnScreen(currentSong->currentClip);

			PadLEDs::setupAudioClipCollapseOrExplodeAnimation(clip);

			PadLEDs::recordTransitionBegin(kClipCollapseSpeed);
			PadLEDs::renderAudioClipExpandOrCollapse();
		}
	}
	else {
		int32_t transitioningToRow = getClipPlaceOnScreen(currentSong->currentClip);
		InstrumentClip* instrumentClip = (InstrumentClip*)currentSong->currentClip;
		if (instrumentClip->onKeyboardScreen) {
			keyboardScreen.renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1], false);
			keyboardScreen.renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1]);

			PadLEDs::numAnimatedRows = kDisplayHeight;
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				PadLEDs::animatedRowGoingTo[y] = transitioningToRow;
				PadLEDs::animatedRowGoingFrom[y] = y;
			}
		}
		else {
			instrumentClipView.renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1],
			                                  false);
			instrumentClipView.renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1]);

			PadLEDs::numAnimatedRows = kDisplayHeight + 2; // I didn't see a difference but the + 2 seems intentional
			for (int32_t y = 0; y < PadLEDs::numAnimatedRows; y++) {
				PadLEDs::animatedRowGoingTo[y] = transitioningToRow;
				PadLEDs::animatedRowGoingFrom[y] = y - 1;
			}
		}

		// Must set this after above render calls, or else they'll see it and not render
		currentUIMode = UI_MODE_INSTRUMENT_CLIP_COLLAPSING;

		// Set occupancy masks to full for the sidebar squares in the Store
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			PadLEDs::occupancyMaskStore[y + 1][kDisplayWidth] = 64;
			PadLEDs::occupancyMaskStore[y + 1][kDisplayWidth + 1] = 64;
		}

		PadLEDs::setupInstrumentClipCollapseAnimation(true);

		if (!instrumentClip->onKeyboardScreen) {
			instrumentClipView.fillOffScreenImageStores();
		}
		PadLEDs::recordTransitionBegin(kClipCollapseSpeed);
		PadLEDs::renderClipExpandOrCollapse();
	}
}

// Might be called during card routine! So renders might fail. Not too likely
void SessionView::finishedTransitioningHere() {
	AudioEngine::routineWithClusterLoading(); // -----------------------------------
	currentUIMode = UI_MODE_ANIMATION_FADE;
	PadLEDs::recordTransitionBegin(kFadeSpeed);
	changeRootUI(this);
	renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight], &PadLEDs::occupancyMaskStore[kDisplayHeight],
	               true);
	renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[kDisplayHeight], &PadLEDs::occupancyMaskStore[kDisplayHeight]);
	PadLEDs::timerRoutine(); // What... why? This would normally get called from that...
}

void SessionView::playbackEnded() {
	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
		requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
		return;
	}

	uint32_t whichRowsToReRender = 0;

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		Clip* clip = getClipOnScreen(yDisplay);
		if (clip && clip->type == CLIP_TYPE_AUDIO) {
			AudioClip* audioClip = (AudioClip*)clip;

			if (!audioClip->sampleHolder.audioFile) {
				whichRowsToReRender |= (1 << yDisplay);
			}
		}
	}

	if (whichRowsToReRender) {
		requestRendering(this, whichRowsToReRender, 0);
	}
}

void SessionView::clipNeedsReRendering(Clip* clip) {
	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
		requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
		return;
	}

	int32_t bottomIndex = currentSong->songViewYScroll;
	int32_t topIndex = bottomIndex + kDisplayHeight;

	bottomIndex = std::max(bottomIndex, 0_i32);
	topIndex = std::min(topIndex, currentSong->sessionClips.getNumElements());

	for (int32_t c = bottomIndex; c < topIndex; c++) {
		Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);
		if (thisClip == clip) {
			int32_t yDisplay = c - currentSong->songViewYScroll;
			requestRendering(this, (1 << yDisplay), 0);
			break;
		}
	}
}

void SessionView::sampleNeedsReRendering(Sample* sample) {
	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
		requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
		return;
	}

	int32_t bottomIndex = currentSong->songViewYScroll;
	int32_t topIndex = bottomIndex + kDisplayHeight;

	bottomIndex = std::max(bottomIndex, 0_i32);
	topIndex = std::min(topIndex, currentSong->sessionClips.getNumElements());

	for (int32_t c = bottomIndex; c < topIndex; c++) {
		Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);
		if (thisClip->type == CLIP_TYPE_AUDIO && ((AudioClip*)thisClip)->sampleHolder.audioFile == sample) {
			int32_t yDisplay = c - currentSong->songViewYScroll;
			requestRendering(this, (1 << yDisplay), 0);
		}
	}
}

void SessionView::midiLearnFlash() {
	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
		requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
		return;
	}

	uint32_t mainRowsToRender = 0;
	uint32_t sideRowsToRender = 0;

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		Clip* clip = getClipOnScreen(yDisplay);
		if (clip) {

			if (clip->muteMIDICommand.containsSomething()
			    || (view.thingPressedForMidiLearn == MidiLearn::CLIP && &clip->muteMIDICommand == view.learnedThing)
			    || currentSong->sections[clip->section].launchMIDICommand.containsSomething()
			    || (view.thingPressedForMidiLearn == MidiLearn::SECTION
			        && view.learnedThing == &currentSong->sections[clip->section].launchMIDICommand)) {
				sideRowsToRender |= (1 << yDisplay);
			}

			if (clip->output->type == InstrumentType::SYNTH || clip->output->type == InstrumentType::MIDI_OUT
			    || clip->output->type == InstrumentType::CV) {

				if (((MelodicInstrument*)clip->output)->midiInput.containsSomething()
				    || (view.thingPressedForMidiLearn == MidiLearn::MELODIC_INSTRUMENT_INPUT
				        && view.learnedThing
				               == &((MelodicInstrument*)clip->output)
				                       ->midiInput)) { // Should be fine even if output isn't a MelodicInstrument

					mainRowsToRender |= (1 << yDisplay);
				}
			}
		}
	}

	requestRendering(this, mainRowsToRender, sideRowsToRender);
}

void SessionView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	performActionOnPadRelease = false;

	if (getCurrentUI() == this) { //This routine may also be called from the Arranger view
		ClipNavigationTimelineView::modEncoderAction(whichModEncoder, offset);
	}
}

Clip* SessionView::getClipForLayout() {
	switch (currentSong->sessionLayout) {
	case SessionLayoutType::SessionLayoutTypeGrid: {
		return gridClipFromCoords(gridFirstPressedX, gridFirstPressedY);
		break;
	}
	case SessionLayoutType::SessionLayoutTypeRows:
	default: {
		return getClipOnScreen(selectedClipYDisplay);
	}
	}
}

void SessionView::selectLayout(int8_t offset) {
	gridSetDefaultMode();
	gridResetPresses();
	gridModeActive = gridModeSelected;

	// Layout change
	if (offset != 0) {
		switch (currentSong->sessionLayout) {
		case SessionLayoutType::SessionLayoutTypeRows: {
			currentSong->sessionLayout = SessionLayoutType::SessionLayoutTypeGrid;
			break;
		}
		case SessionLayoutType::SessionLayoutTypeGrid: {
			currentSong->sessionLayout = SessionLayoutType::SessionLayoutTypeRows;
			break;
		}
		}

		// After change
		if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeRows) {
			display->displayPopup("Rows");
			selectedClipYDisplay = 255;
			currentSong->songViewYScroll = (currentSong->sessionClips.getNumElements() - kDisplayHeight);
		}
		else if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
			display->displayPopup("Grid");
			currentSong->songGridScrollX = 0;
			currentSong->songGridScrollY = 0;
		}

		requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
		view.flashPlayEnable();
	}
}

bool SessionView::gridRenderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {

	// Section column
	uint32_t sectionColumnIndex = kDisplayWidth;
	for (int32_t y = (kGridHeight - 1); y >= 0; --y) {
		occupancyMask[y][sectionColumnIndex] = 64;

		auto section = gridSectionFromY(y);
		auto* ptrSectionColour = image[y][sectionColumnIndex];

		hueToRGB(defaultClipGroupColours[gridSectionFromY(y)], ptrSectionColour);
		colorCopy(ptrSectionColour, ptrSectionColour, 255, 2);

		if (view.midiLearnFlashOn && gridModeActive == SessionGridModeLaunch) {
			// MIDI colour if necessary
			if (currentSong->sections[section].launchMIDICommand.containsSomething()) {
				ptrSectionColour[0] = midiCommandColour.r;
				ptrSectionColour[1] = midiCommandColour.g;
				ptrSectionColour[2] = midiCommandColour.b;
			}

			else {
				// If user assigning MIDI controls and has this section selected, flash to half brightness
				if (currentSong && view.learnedThing == &currentSong->sections[section].launchMIDICommand) {
					ptrSectionColour[0] >>= 1;
					ptrSectionColour[1] >>= 1;
					ptrSectionColour[2] >>= 1;
				}
			}
		}

		// Action modes column
		uint32_t actionModeColumnIndex = kDisplayWidth + 1;
		bool modeExists = true;
		bool modeActive = false;
		uint8_t modeColour[3] = {0};
		switch (y) {
		case 7: {
			modeActive = (gridModeActive == SessionGridModeLaunch);
			modeColour[1] = 255; // Green
			break;
		}
		case 6: {
			modeActive = (gridModeActive == SessionGridModeEdit);
			modeColour[2] = 255; // Blue
			break;
		}

		default: {
			modeExists = false;
			break;
		}
		}
		occupancyMask[y][actionModeColumnIndex] = (modeExists ? 1 : 0);
		colorCopy(image[y][actionModeColumnIndex], &modeColour[0], 255, (modeActive ? 1 : 8));
	}

	return true;
}

bool SessionView::gridRenderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                     uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {

	// We currently assume sidebar is rendered after main pads
	memset(image, 0, sizeof(uint8_t) * kDisplayHeight * (kDisplayWidth + kSideBarWidth) * 3);

	// Iterate over all clips and render them where they are
	auto trackCount = gridTrackCount();

	PadLEDs::renderingLock = true;

	for (int32_t idxClip = 0; idxClip < currentSong->sessionClips.getNumElements(); ++idxClip) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
		auto trackIndex = gridTrackIndexFromTrack(clip->output, trackCount);
		if (trackIndex < 0) {
			uartPrintln("Global output list mismatch");
			continue; // Should never happen but theoretically global output list can diverge from clip pointers
		}

		auto x = gridXFromTrack(trackIndex);
		auto y = gridYFromSection(clip->section);

		// Render colour for every valid clip
		if (x >= 0 && y >= 0) {
			occupancyMask[y][x] = 64;
			gridRenderClipColor(clip, image[y][x]);
		}
	}

	PadLEDs::renderingLock = false;

	return true;
}

void SessionView::gridRenderClipColor(Clip* clip, uint8_t resultColour[]) {
	// Greyout all clips during record button pressed or soloing, overwrite for clips that shouldn't be greyed out
	bool greyout = (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING) || currentSong->getAnyClipsSoloing();

	// Handle record button pressed
	if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING && clip->armedForRecording) {
		if (view.blinkOn) {
			bool shouldGoPurple = (clip->type == CLIP_TYPE_AUDIO && ((AudioClip*)clip)->overdubsShouldCloneOutput);

			// Bright colour
			if (clip->wantsToBeginLinearRecording(currentSong)) {
				if (shouldGoPurple) {
					resultColour[0] = 128;
					resultColour[1] = 0;
					resultColour[2] = 128;
				}
				else {
					resultColour[0] = 255;
					resultColour[1] = 1;
					resultColour[2] = 0;
				}
			}
			// Dull colour, cos can't actually begin linear recording despite being armed
			else {
				if (shouldGoPurple) {
					resultColour[0] = 60;
					resultColour[1] = 15;
					resultColour[2] = 60;
				}
				else {
					resultColour[0] = 60;
					resultColour[1] = 15;
					resultColour[2] = 15;
				}
			}
		}
		return;
	}

	// MIDI Learning
	if (view.midiLearnFlashOn) {
		if (gridModeActive == SessionGridModeLaunch) {
			// Clip arm learned
			if (clip->muteMIDICommand.containsSomething()) {
				resultColour[0] = midiCommandColour.r;
				resultColour[1] = midiCommandColour.g;
				resultColour[2] = midiCommandColour.b;
				return;
			}
			// Selected but unlearned
			else if (view.learnedThing == &clip->muteMIDICommand) {
				return; // Flash black
			}
		}
		else if (gridModeActive == SessionGridModeEdit) {
			// Instrument learned
			InstrumentType type = clip->output->type;
			bool canLearn =
			    (type == InstrumentType::SYNTH || type == InstrumentType::MIDI_OUT || type == InstrumentType::CV);
			if (canLearn && ((MelodicInstrument*)clip->output)->midiInput.containsSomething()) {
				resultColour[0] = midiCommandColour.r;
				resultColour[1] = midiCommandColour.g;
				resultColour[2] = midiCommandColour.b;
				return;
			}

			// Selected but unlearned
			else if (view.thingPressedForMidiLearn == MidiLearn::MELODIC_INSTRUMENT_INPUT
			         && view.learnedThing == &((MelodicInstrument*)clip->output)->midiInput) {
				return; // Flash black
			}
		}
	}

	// Black phase of arm flashing
	if (view.clipArmFlashOn && clip->armState != ArmState::OFF) {
		return;
	}

	// Set a random color if unset and convert to result colour
	if (clip->output->colour == 0) {
		lastColour = std::fmod(lastColour + colourStep + 192, 192);
		clip->output->colour = lastColour;
	}
	hueToRGB(clip->output->colour, resultColour);

	// If we are not in record arming mode make this clip full color for being soloed
	if ((clip->soloingInSessionMode || clip->armState == ArmState::ON_TO_SOLO)
	    && currentUIMode != UI_MODE_VIEWING_RECORD_ARMING) {
		greyout = false;
	}

	// If clip is not active or grayed out - dim it
	else if (!clip->activeIfNoSolo) {
		resultColour[0] = ((float)resultColour[0] / 255) * 10;
		resultColour[1] = ((float)resultColour[1] / 255) * 10;
		resultColour[2] = ((float)resultColour[2] / 255) * 10;
	}

	if (greyout) {
		greyColourOut(resultColour, resultColour, 6500000);
	}
}

Clip* SessionView::gridCloneClip(Clip* sourceClip) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithSong(modelStackMemory, currentSong)->addTimelineCounter(sourceClip);

	int32_t error = sourceClip->clone(modelStack, false);
	if (error) {
		display->displayError(ERROR_INSUFFICIENT_RAM);
		return nullptr;
	}

	return (Clip*)modelStack->getTimelineCounter();
}

Clip* SessionView::gridCreateClipInTrack(Output* targetOutput) {
	Clip* sourceClip = nullptr;
	for (int32_t idxClip = 0; idxClip < currentSong->sessionClips.getNumElements(); ++idxClip) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
		if (clip->output == targetOutput) {
			sourceClip = clip;
			break;
		}
	}

	if (sourceClip == nullptr && targetOutput->activeClip != nullptr) {
		sourceClip = targetOutput->activeClip;
	}

	if (sourceClip == nullptr) {
		return nullptr;
	}

	// New method is cloning full clip and emptying it
	Clip* newClip = gridCloneClip(sourceClip);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, newClip);
	Action* action = actionLogger.getNewAction(ACTION_CLIP_CLEAR, false);
	newClip->clear(action, modelStack);
	actionLogger.deleteAllLogs();

	// For safety we set it up exactly as we want it
	newClip->colourOffset = random(72);
	newClip->loopLength = currentSong->getBarLength();
	newClip->activeIfNoSolo = false;
	newClip->soloingInSessionMode = false;
	newClip->wasActiveBefore = false;
	newClip->isPendingOverdub = false;
	newClip->isUnfinishedAutoOverdub = false;
	newClip->armState = ArmState::OFF;

	return newClip;
}

bool SessionView::gridCreateNewTrackForClip(InstrumentType type, InstrumentClip* clip, bool copyDrumsFromClip) {
	bool instrumentAlreadyInSong = false;
	if (type == InstrumentType::SYNTH || type == InstrumentType::KIT) {
		int32_t error = setPresetOrNextUnlaunchedOne(clip, type, &instrumentAlreadyInSong, copyDrumsFromClip);
		if (error || instrumentAlreadyInSong) {
			if (error) {
				display->displayError(error);
			}
			return false;
		}
	}
	else if (type == InstrumentType::MIDI_OUT || type == InstrumentType::CV) {
		clip->output = currentSong->getNonAudioInstrumentToSwitchTo(type, Availability::INSTRUMENT_UNUSED, 0, -1,
		                                                            &instrumentAlreadyInSong);
		if (clip->output == nullptr) {
			return false;
		}

		auto error = clip->setNonAudioInstrument((Instrument*)(clip->output), currentSong);
		if (error) {
			display->displayError(error);
			return false;
		}
	}
	else {
		return false;
	}

	if (!instrumentAlreadyInSong) {
		currentSong->addOutput(clip->output);
	}

	if (!clip->output->activeClip) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

		clip->output->setActiveClip(modelStackWithTimelineCounter);
	}

	return true;
}

InstrumentClip* SessionView::gridCreateClipWithNewTrack(InstrumentType type) {
	// Allocate new clip
	void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(InstrumentClip));
	if (!memory) {
		display->displayError(ERROR_INSUFFICIENT_RAM);
		return nullptr;
	}

	InstrumentClip* newClip = new (memory) InstrumentClip(currentSong);
	if (!gridCreateNewTrackForClip(type, newClip, true)) {
		newClip->~InstrumentClip();
		delugeDealloc(memory);
		return nullptr;
	}

	// For safety we set it up exactly as we want it
	newClip->colourOffset = random(72);
	newClip->loopLength = currentSong->getBarLength();
	newClip->activeIfNoSolo = false;
	newClip->soloingInSessionMode = false;
	newClip->wasActiveBefore = false;
	newClip->isPendingOverdub = false;
	newClip->isUnfinishedAutoOverdub = false;
	newClip->armState = ArmState::OFF;

	return newClip;
}

Clip* SessionView::gridCreateClip(uint32_t targetSection, Output* targetOutput, Clip* sourceClip) {
	actionLogger.deleteAllLogs();

	Clip* newClip = nullptr;

	// From source
	if (sourceClip != nullptr) {
		// Can't convert between audio and non audio tracks
		if (targetOutput) {
			bool sourceIsAudio = (sourceClip->output->type == InstrumentType::AUDIO);
			bool targetIsAudio = (targetOutput->type == InstrumentType::AUDIO);
			if (sourceIsAudio != targetIsAudio) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_CANT_CONVERT_TYPE));
				return nullptr;
			}
		}

		// First we make an identical copy
		newClip = gridCloneClip(sourceClip);
		if (newClip == nullptr) {
			return nullptr;
		}
	}

	// Create new clip in existing track
	else if (targetOutput != nullptr) {
		newClip = gridCreateClipInTrack(targetOutput);
	}

	// Create new clip in new track
	else {
		// This is the right position to add immediate type creation
		newClip = gridCreateClipWithNewTrack(InstrumentType::SYNTH);
	}

	// Set new clip section and add it to the list
	if (newClip == nullptr) {
		return nullptr;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithSong(modelStackMemory, currentSong)->addTimelineCounter(newClip);

	newClip->section = targetSection;
	if (newClip->type == CLIP_TYPE_INSTRUMENT) {
		((InstrumentClip*)newClip)->onKeyboardScreen = false;
	}

	if (currentSong->sessionClips.insertClipAtIndex(newClip, 0) != NO_ERROR) {
		newClip->~Clip();
		delugeDealloc(newClip);
		display->displayError(ERROR_INSUFFICIENT_RAM);
		return nullptr;
	}

	// If we copied from source and the clip should go in another track we need to move it after putting it in the session
	// Remember this assumes a non Audio clip
	if (sourceClip != nullptr) {
		if (sourceClip->type == CLIP_TYPE_INSTRUMENT) {
			InstrumentClip* newInstrumentClip = (InstrumentClip*)newClip;
			// Create a new track for the clip
			if (targetOutput == nullptr) {
				if (!gridCreateNewTrackForClip(sourceClip->output->type, newInstrumentClip, false)) {
					currentSong->sessionClips.deleteAtIndex(0);
					newClip->~Clip();
					delugeDealloc(newClip);
					return nullptr;
				}

				targetOutput = newInstrumentClip->output;
			}

			// Different instrument, switch the cloned clip to it
			else if (targetOutput != sourceClip->output) {
				int32_t error = newInstrumentClip->changeInstrument(modelStack, (Instrument*)targetOutput, NULL,
				                                                    InstrumentRemoval::NONE);
				if (error != NO_ERROR) {
					display->displayPopup(l10n::get(l10n::String::STRING_FOR_SWITCHING_TO_TRACK_FAILED));
				}

				if (targetOutput->type == InstrumentType::KIT) {
					newInstrumentClip->yScroll = 0;
				}
			}
		}

		else if (sourceClip->type == CLIP_TYPE_AUDIO) {
			AudioClip* newAudioClip = (AudioClip*)newClip;

			if (targetOutput == nullptr) {
				AudioOutput* newOutput = currentSong->createNewAudioOutput();
				if (!newOutput) {
					display->displayPopup(l10n::get(l10n::String::STRING_FOR_SWITCHING_TO_TRACK_FAILED));
				}
				else {
					targetOutput = newOutput;
				}
			}

			if (targetOutput && targetOutput != sourceClip->output) {
				newAudioClip->setOutput(modelStack, targetOutput);
			}
		}
	}

	// Figure out the play pos for the new Clip if we're currently playing
	if (session.hasPlaybackActive() && playbackHandler.isEitherClockActive() && currentSong->isClipActive(newClip)) {
		session.reSyncClip(modelStack, true);
	}

	// Set to active for new tracks
	if (targetOutput == nullptr && !newClip->output->activeClip) {
		newClip->output->setActiveClip(modelStack);
	}

	return newClip;
}

void SessionView::gridClonePad(uint32_t sourceX, uint32_t sourceY, uint32_t targetX, uint32_t targetY) {
	Clip* sourceClip = gridClipFromCoords(sourceX, sourceY);
	if (sourceClip == nullptr) {
		return;
	}

	// Don't allow copying recording clips
	if (sourceClip->getCurrentlyRecordingLinearly()) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_CANT_CLONE_AUDIO_IN_OTHER_TRACK));
		return;
	}

	Clip* targetClip = gridClipFromCoords(targetX, targetY);
	if (targetClip != nullptr) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_TARGET_FULL));
		return;
	}

	gridCreateClip(gridSectionFromY(targetY), gridTrackFromX(targetX, gridTrackCount()), sourceClip);
}

void SessionView::gridStartSection(uint32_t section, bool instant) {
	if (instant) {
		currentSong->turnSoloingIntoJustPlaying(currentSong->sections[section].numRepetitions != -1);

		for (int32_t idxClip = 0; idxClip < currentSong->sessionClips.getNumElements(); ++idxClip) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);

			if ((clip->section == section && !clip->activeIfNoSolo)
			    || (clip->section != section && clip->activeIfNoSolo)) {
				gridToggleClipPlay(clip, instant);
			}
			else {
				clip->armState = ArmState::OFF;
			}
		}

		session.launchSchedulingMightNeedCancelling();
	}
	else {
		session.armSection(section, kInternalButtonPressLatency);
	}
}

void SessionView::gridToggleClipPlay(Clip* clip, bool instant) {
	session.toggleClipStatus(clip, nullptr, instant, kInternalButtonPressLatency);
}

ActionResult SessionView::gridHandlePads(int32_t x, int32_t y, int32_t on) {
	// Except for the path to sectionPadAction in the original function all paths contained this check. Can probably be refactored
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	if (currentUIMode == UI_MODE_EXPLODE_ANIMATION) {
		return ActionResult::DEALT_WITH;
	}

	// Right sidebar column - action modes
	if (x > kDisplayWidth) {
		clipPressEnded();

		if (on) {
			gridActiveModeUsed = false;
			switch (y) {
			case 7: {
				gridModeActive = SessionGridModeLaunch;
				break;
			}
			case 6: {
				gridModeActive = SessionGridModeEdit;
				break;
			}
			}
		}
		else {
			if (FlashStorage::defaultGridActiveMode == GridDefaultActiveModeSelection) {
				if (!gridActiveModeUsed) {
					gridModeSelected = gridModeActive;
				}
			}
			else {
				gridSetDefaultMode();
			}

			gridModeActive = gridModeSelected;
		}
	}
	else {
		gridActiveModeUsed = true;

		Clip* clip = gridClipFromCoords(x, y);
		ActionResult modeHandleResult = ActionResult::NOT_DEALT_WITH;
		switch (gridModeActive) {
		case SessionGridModeEdit: {
			modeHandleResult = gridHandlePadsEdit(x, y, on, clip);
			break;
		}
		case SessionGridModeLaunch: {
			modeHandleResult = gridHandlePadsLaunch(x, y, on, clip);
			break;
		}
		}

		if (modeHandleResult == ActionResult::DEALT_WITH) {
			return ActionResult::DEALT_WITH;
		}
	}

	if (currentUIMode != UI_MODE_EXPLODE_ANIMATION) {
		requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
		view.flashPlayEnable();
	}

	return ActionResult::DEALT_WITH;
}

ActionResult SessionView::gridHandlePadsEdit(int32_t x, int32_t y, int32_t on, Clip* clip) {
	// Left sidebar column (sections)
	if (x == kDisplayWidth) {
		// Get pressed section
		auto section = gridSectionFromY(y);
		if (section < 0) {
			return ActionResult::DEALT_WITH;
		}

		// Immediate release of the pad arms the section, holding allows changing repeats
		if (on) {
			enterUIMode(UI_MODE_HOLDING_SECTION_PAD);
			sectionPressed = section;
			beginEditingSectionRepeatsNum();
		}
		else {
			if (isUIModeActive(UI_MODE_HOLDING_SECTION_PAD)) {
				exitUIMode(UI_MODE_HOLDING_SECTION_PAD);
				if (display->haveOLED()) {
					deluge::hid::display::OLED::removePopup();
				}
				else {
					redrawNumericDisplay();
				}
			}
		}

		return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
	}

	// Learn MIDI for tracks
	if (currentUIMode == UI_MODE_MIDI_LEARN) {
		if (clip != nullptr) {
			if (clip->type != CLIP_TYPE_AUDIO) {
				// Learn + Holding pad = Learn MIDI channel
				Output* output = gridTrackFromX(x, gridTrackCount());
				if (output
				    && (output->type == InstrumentType::SYNTH || output->type == InstrumentType::MIDI_OUT
				        || output->type == InstrumentType::CV)) {
					view.melodicInstrumentMidiLearnPadPressed(on, (MelodicInstrument*)output);
				}
			}
			else {
				view.endMIDILearn();
				gui::context_menu::audioInputSelector.audioOutput = (AudioOutput*)clip->output;
				gui::context_menu::audioInputSelector.setupAndCheckAvailability();
				openUI(&gui::context_menu::audioInputSelector);
				return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
			}
		}

		return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
	}

	if (on) {
		// Only do this if no pad is pressed yet
		if (gridFirstPressedX == -1 && gridFirstPressedY == -1) {
			gridFirstPressedX = x;
			gridFirstPressedY = y;

			// Create new track on empty slots
			if (clip == nullptr) {
				uint32_t trackCount = gridTrackCount();
				auto trackIndex = gridTrackIndexFromX(x, trackCount);

				// Create clip if it does not exist
				if ((x + currentSong->songGridScrollX) <= trackCount) {
					Output* track = gridTrackFromX(x, trackCount);
					clip = gridCreateClip(gridSectionFromY(y), track, nullptr);
					// Immediately start playing it for new tracks
					if (clip != nullptr && track == nullptr) {
						gridToggleClipPlay(clip, true);
					}
				}
			}

			if (clip == nullptr) {
				return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
			}

			// Allow clip control (selection)
			currentUIMode = UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
			performActionOnPadRelease = true;
			selectedClipTimePressed = AudioEngine::audioSampleTimer;
			view.setActiveModControllableTimelineCounter(clip);
			view.displayOutputName(clip->output, true, clip);
			if (display->haveOLED()) {
				deluge::hid::display::OLED::sendMainImage();
			}
		}
		// Remember the second press down if empty
		else if (gridSecondPressedX == -1 || gridSecondPressedY == -1) {
			performActionOnPadRelease = false;
			gridSecondPressedX = x;
			gridSecondPressedY = y;
		}
	}
	// Release
	else {
		// First finger up
		if (gridFirstPressedX == x && gridFirstPressedY == y) {
			// Open clip if no other pad was previously pressed, timer has not run out and clip is pressed
			if (isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) && performActionOnPadRelease
			    && AudioEngine::audioSampleTimer - selectedClipTimePressed < kShortPressTime) {

				// Not allowed if recording arrangement
				if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RECORDING_TO_ARRANGEMENT));
				}
				else {
					if (clip != nullptr) {
						transitionToViewForClip(clip);
					}
					return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
				}
			}

			clipPressEnded();
		}

		// Second finger up, clone clip
		else if (gridSecondPressedX == x && gridSecondPressedY == y) {
			gridClonePad(gridFirstPressedX, gridFirstPressedY, gridSecondPressedX, gridSecondPressedY);
			gridResetPresses(false, true);
		}
	}

	return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
}

ActionResult SessionView::gridHandlePadsLaunch(int32_t x, int32_t y, int32_t on, Clip* clip) {
	if (on && playbackHandler.playbackState && currentPlaybackMode == &arrangement) {
		if (currentUIMode == UI_MODE_NONE) {
			playbackHandler.switchToSession();
		}

		return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
	}

	// Left sidebar column (sections)
	if (x == kDisplayWidth) {
		// Get pressed section
		auto section = gridSectionFromY(y);
		if (section < 0) {
			return ActionResult::DEALT_WITH;
		}

		// MIDI learn section
		if (currentUIMode == UI_MODE_MIDI_LEARN) {
			view.sectionMidiLearnPadPressed(on, section);
			return ActionResult::DEALT_WITH;
		}

		if (on) {
			// Immediate launch if shift pressed
			gridStartSection(section, Buttons::isShiftButtonPressed());
		}

		return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
	}

	if (clip == nullptr) {
		if (on && currentUIMode == UI_MODE_NONE && FlashStorage::gridUnarmEmptyPads) {
			auto maxTrack = gridTrackCount();
			Output* track = gridTrackFromX(x, maxTrack);
			if (track != nullptr) {
				for (int32_t idxClip = 0; idxClip < currentSong->sessionClips.getNumElements(); ++idxClip) {
					Clip* sessionClip = currentSong->sessionClips.getClipAtIndex(idxClip);
					if (sessionClip->output == track) {
						if (sessionClip->activeIfNoSolo) {
							gridToggleClipPlay(sessionClip, Buttons::isShiftButtonPressed());
						}
						else {
							sessionClip->armState = ArmState::OFF;
						}
					}
				}

				return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
			}
		}

		return ActionResult::DEALT_WITH;
	}

	// Learn MIDI ARM
	if (currentUIMode == UI_MODE_MIDI_LEARN) {
		view.clipStatusMidiLearnPadPressed(on, clip);
		return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
	}

	if (FlashStorage::gridAllowGreenSelection) {
		return gridHandlePadsLaunchWithSelection(x, y, on, clip);
	}
	else {
		return gridHandlePadsLaunchImmediate(x, y, on, clip);
	}
}

ActionResult SessionView::gridHandlePadsLaunchImmediate(int32_t x, int32_t y, int32_t on, Clip* clip) {
	// From here all actions only happen on press
	if (!on) {
		return ActionResult::DEALT_WITH;
	}

	gridHandlePadsLaunchToggleArming(clip, Buttons::isShiftButtonPressed());

	return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
}

ActionResult SessionView::gridHandlePadsLaunchWithSelection(int32_t x, int32_t y, int32_t on, Clip* clip) {
	if (on) {
		// Immediate arming, immediate consumption
		if (Buttons::isShiftButtonPressed()) {
			gridHandlePadsLaunchToggleArming(clip, true);
			return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
		}

		if (gridFirstPressedX == -1 && gridFirstPressedY == -1) {
			gridFirstPressedX = x;
			gridFirstPressedY = y;

			// Allow clip control (selection)
			currentUIMode = UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
			performActionOnPadRelease = true;
			selectedClipTimePressed = AudioEngine::audioSampleTimer;
			view.setActiveModControllableTimelineCounter(clip);
			view.displayOutputName(clip->output, true, clip);
			if (display->haveOLED()) {
				deluge::hid::display::OLED::sendMainImage();
			}
		}
		// Special case, if there are already selected pads we allow immediate arming all others
		else {
			return gridHandlePadsLaunchImmediate(x, y, on, clip);
		}
	}
	else {
		if (gridFirstPressedX == x && gridFirstPressedY == y) {
			if (isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) && performActionOnPadRelease
			    && AudioEngine::audioSampleTimer - selectedClipTimePressed < kShortPressTime) {

				gridHandlePadsLaunchToggleArming(clip, false);
			}

			clipPressEnded();
		}
	}

	return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
}

void SessionView::gridHandlePadsLaunchToggleArming(Clip* clip, bool immediate) {
	if (immediate) {
		if (currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
			session.soloClipAction(clip, kInternalButtonPressLatency);
		}
		else {
			gridToggleClipPlay(clip, true);
		}
	}
	else {
		if (currentUIMode == UI_MODE_VIEWING_RECORD_ARMING) {
			// Here I removed the overdubbing settings
			clip->armedForRecording = !clip->armedForRecording;
			PadLEDs::reassessGreyout(true);
		}
		else if (currentUIMode == UI_MODE_NONE && Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
			clip->armedForRecording = !clip->armedForRecording;
			sessionView.timerCallback();
		}
		else if ((currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW
		          || currentUIMode == UI_MODE_STUTTERING)) {
			gridToggleClipPlay(clip, false);
		}
		else if (currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
			session.soloClipAction(clip, kInternalButtonPressLatency);
			// Make sure we can mute additional pads after this and don't loose UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON
		}
	}
}

ActionResult SessionView::gridHandleScroll(int32_t offsetX, int32_t offsetY) {
	if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
		display->cancelPopup();
	}

	if (currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW && offsetY != 0) {
		auto track = gridTrackFromX(gridFirstPressedX, gridTrackCount());
		if (track != nullptr) {
			if (Buttons::isButtonPressed(hid::button::Y_ENC)) {
				track->colour += offsetY;
				if (track->colour == 0) {
					track->colour += offsetY;
				}
			}
			else {
				track->colour = static_cast<int16_t>(track->colour + (colourStep * offsetY) + 192) % 192;
			}
			requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
		}

		return ActionResult::DEALT_WITH;
	}

	gridResetPresses();
	clipPressEnded();

	// Fix the range
	currentSong->songGridScrollY =
	    std::clamp<int32_t>(currentSong->songGridScrollY - offsetY, 0, kMaxNumSections - kGridHeight);
	currentSong->songGridScrollX = std::clamp<int32_t>(currentSong->songGridScrollX + offsetX, 0,
	                                                   std::max<int32_t>(0, (gridTrackCount() - kDisplayWidth) + 1));

	// This is the right place to add new features like moving clips or tracks :)

	requestRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
	view.flashPlayEnable();
	return ActionResult::DEALT_WITH;
}

void SessionView::gridTransitionToSessionView() {
	Sample* sample;

	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {
		// If no sample, just skip directly there
		if (!((AudioClip*)currentSong->currentClip)->sampleHolder.audioFile) {
			changeRootUI(&sessionView);
			memcpy(PadLEDs::imageStore, PadLEDs::image, sizeof(PadLEDs::image));
			finishedTransitioningHere();
			return;
		}
	}

	currentUIMode = UI_MODE_EXPLODE_ANIMATION;

	memcpy(PadLEDs::imageStore[1], PadLEDs::image, (kDisplayWidth + kSideBarWidth) * kDisplayHeight * 3);
	memcpy(PadLEDs::occupancyMaskStore[1], PadLEDs::occupancyMask, (kDisplayWidth + kSideBarWidth) * kDisplayHeight);
	if (getCurrentUI() == &instrumentClipView) {
		instrumentClipView.fillOffScreenImageStores();
	}

	auto clipX = std::clamp<int32_t>(
	    gridXFromTrack(gridTrackIndexFromTrack(currentSong->currentClip->output, gridTrackCount())), 0, kDisplayWidth);
	auto clipY = std::clamp<int32_t>(gridYFromSection(currentSong->currentClip->section), 0, kDisplayHeight);

	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {
		waveformRenderer.collapseAnimationToWhichRow = clipY;

		PadLEDs::setupAudioClipCollapseOrExplodeAnimation((AudioClip*)currentSong->currentClip);
	}
	else {
		PadLEDs::explodeAnimationYOriginBig = clipY << 16;
	}

	PadLEDs::explodeAnimationXStartBig = clipX << 16;
	PadLEDs::explodeAnimationXWidthBig = (1 << 16);

	PadLEDs::recordTransitionBegin(kClipCollapseSpeed);
	PadLEDs::explodeAnimationDirection = -1;

	if (getCurrentUI() == &instrumentClipView) {
		PadLEDs::clearSideBar();
	}

	PadLEDs::explodeAnimationTargetUI = this;
	uiTimerManager.setTimer(TIMER_MATRIX_DRIVER, 35);
}

void SessionView::gridTransitionToViewForClip(Clip* clip) {
	if (clip->type == CLIP_TYPE_AUDIO) {
		// If no sample, just skip directly there
		if (!((AudioClip*)clip)->sampleHolder.audioFile) {
			currentUIMode = UI_MODE_NONE;
			changeRootUI(&audioClipView);
			return;
		}
	}

	currentUIMode = UI_MODE_EXPLODE_ANIMATION;

	auto clipX = std::clamp<int32_t>(
	    gridXFromTrack(gridTrackIndexFromTrack(currentSong->currentClip->output, gridTrackCount())), 0, kDisplayWidth);
	auto clipY = std::clamp<int32_t>(gridYFromSection(currentSong->currentClip->section), 0, kDisplayHeight);

	if (clip->type == CLIP_TYPE_AUDIO) {
		waveformRenderer.collapseAnimationToWhichRow = clipY;

		int64_t xScrollSamples;
		int64_t xZoomSamples;

		((AudioClip*)clip)
		    ->getScrollAndZoomInSamples(currentSong->xScroll[NAVIGATION_CLIP], currentSong->xZoom[NAVIGATION_CLIP],
		                                &xScrollSamples, &xZoomSamples);

		waveformRenderer.findPeaksPerCol((Sample*)((AudioClip*)clip)->sampleHolder.audioFile, xScrollSamples,
		                                 xZoomSamples, &((AudioClip*)clip)->renderData);

		PadLEDs::setupAudioClipCollapseOrExplodeAnimation((AudioClip*)clip);
	}

	else {
		PadLEDs::explodeAnimationYOriginBig = clipY << 16;

		// If going to KeyboardView...
		if (((InstrumentClip*)clip)->onKeyboardScreen) {
			keyboardScreen.renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1]);
			memset(PadLEDs::occupancyMaskStore[0], 0, kDisplayWidth + kSideBarWidth);
			memset(PadLEDs::occupancyMaskStore[kDisplayHeight + 1], 0, kDisplayWidth + kSideBarWidth);
		}

		// Or if just regular old InstrumentClipView
		else {
			instrumentClipView.recalculateColours();
			instrumentClipView.renderMainPads(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1],
			                                  false);
			instrumentClipView.renderSidebar(0xFFFFFFFF, &PadLEDs::imageStore[1], &PadLEDs::occupancyMaskStore[1]);

			instrumentClipView.fillOffScreenImageStores();
		}
	}

	int32_t start = instrumentClipView.getPosFromSquare(0);
	int32_t end = instrumentClipView.getPosFromSquare(kDisplayWidth);

	PadLEDs::explodeAnimationXStartBig = clipX << 16;
	PadLEDs::explodeAnimationXWidthBig = 1 << 16;

	PadLEDs::recordTransitionBegin(kClipCollapseSpeed);
	PadLEDs::explodeAnimationDirection = 1;

	if (clip->type == CLIP_TYPE_AUDIO) {
		PadLEDs::renderAudioClipExplodeAnimation(0);
	}
	else {
		PadLEDs::renderExplodeAnimation(0);
	}

	PadLEDs::sendOutSidebarColours(); // They'll have been cleared by the first explode render
}

const uint32_t SessionView::gridTrackCount() {
	uint32_t count = 0;
	Output* currentTrack = currentSong->firstOutput;
	while (currentTrack != nullptr) {
		if (currentTrack->activeClip != nullptr) {
			++count;
		}
		currentTrack = currentTrack->next;
	}

	return count;
}

uint32_t SessionView::gridClipCountForTrack(Output* track) {
	uint32_t count = 0;
	for (int32_t idxClip = 0; idxClip < currentSong->sessionClips.getNumElements(); ++idxClip) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
		if (clip->output == track) {
			++count;
		}
	}

	return count;
}

uint32_t SessionView::gridTrackIndexFromTrack(Output* track, uint32_t maxTrack) {
	if (maxTrack <= 0) {
		return -1;
	}

	uint32_t reverseOutputIndex = 0;
	for (Output* ptrOutput = currentSong->firstOutput; ptrOutput; ptrOutput = ptrOutput->next) {
		if (ptrOutput == track) {
			return ((maxTrack - 1) - reverseOutputIndex);
		}
		if (ptrOutput->activeClip != nullptr) {
			++reverseOutputIndex;
		}
	}
	return -1;
}

Output* SessionView::gridTrackFromIndex(uint32_t trackIndex, uint32_t maxTrack) {
	uint32_t count = 0;
	Output* currentTrack = currentSong->firstOutput;
	while (currentTrack != nullptr) {
		if (currentTrack->activeClip != nullptr) {
			if (((maxTrack - 1) - count) == trackIndex) {
				return currentTrack;
			}

			++count;
		}
		currentTrack = currentTrack->next;
	}

	return nullptr;
}

int32_t SessionView::gridYFromSection(uint32_t section) {
	int32_t result = (kGridHeight - 1) - section + currentSong->songGridScrollY;
	if (result >= kGridHeight) {
		return -1;
	}

	return result;
}

int32_t SessionView::gridSectionFromY(uint32_t y) {
	int32_t result = ((kGridHeight - 1) - y) + currentSong->songGridScrollY;
	if (result >= kMaxNumSections) {
		return -1;
	}

	return result;
}

int32_t SessionView::gridXFromTrack(uint32_t trackIndex) {
	int32_t result = trackIndex - currentSong->songGridScrollX;
	if (result >= kDisplayWidth) {
		return -1;
	}

	return result;
}

int32_t SessionView::gridTrackIndexFromX(uint32_t x, uint32_t maxTrack) {
	if (maxTrack <= 0) {
		return 0;
	}
	int32_t result = x + currentSong->songGridScrollX;
	if (result >= maxTrack) {
		return -1;
	}

	return result;
}

Output* SessionView::gridTrackFromX(uint32_t x, uint32_t maxTrack) {
	auto trackIndex = gridTrackIndexFromX(x, maxTrack);
	if (trackIndex < 0) {
		return nullptr;
	}

	return gridTrackFromIndex(trackIndex, maxTrack);
}

Clip* SessionView::gridClipFromCoords(uint32_t x, uint32_t y) {
	auto maxTrack = gridTrackCount();
	Output* track = gridTrackFromX(x, maxTrack);
	if (track == nullptr) {
		return nullptr;
	}

	auto section = gridSectionFromY(y);
	if (section == -1) {
		return nullptr;
	}

	for (int32_t idxClip = 0; idxClip < currentSong->sessionClips.getNumElements(); ++idxClip) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
		if (clip->output == track && clip->section == section) {
			return clip;
		}
	}

	return nullptr;
}
