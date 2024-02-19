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

#include "gui/views/audio_clip_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip_minder.h"
#include "model/consequence/consequence_clip_length.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/playback_mode.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"

extern "C" {
extern uint8_t currentlyAccessingCard;
}

using namespace deluge::gui;

AudioClipView audioClipView{};

AudioClipView::AudioClipView() {
}

inline Sample* getSample() {
	if (getCurrentAudioClip()->getCurrentlyRecordingLinearly()) {
		return getCurrentAudioClip()->recorder->sample;
	}
	else {
		return (Sample*)getCurrentAudioClip()->sampleHolder.audioFile;
	}
}

bool AudioClipView::opened() {
	mustRedrawTickSquares = true;
	uiNeedsRendering(this);

	getCurrentClip()->onAutomationClipView = false;

	focusRegained();
	return true;
}

void AudioClipView::focusRegained() {
	ClipView::focusRegained();
	endMarkerVisible = false;
	indicator_leds::setLedState(IndicatorLED::BACK, false);
	view.focusRegained();
	view.setActiveModControllableTimelineCounter(getCurrentClip());

	if (display->have7SEG()) {
		view.displayOutputName(getCurrentOutput(), false);
	}
#ifdef currentClipStatusButtonX
	view.drawCurrentClipPad(getCurrentClip());
#endif
}

void AudioClipView::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	view.displayOutputName(getCurrentOutput(), false);
}

bool AudioClipView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)) {
		return true;
	}

	int32_t endSquareDisplay = divide_round_negative(
	    getCurrentAudioClip()->loopLength - currentSong->xScroll[NAVIGATION_CLIP] - 1,
	    currentSong->xZoom[NAVIGATION_CLIP]); // Rounds it well down, so we get the "final square" kinda...

	// If no Sample, just clear display
	if (!getSample()) {

		for (int32_t y = 0; y < kDisplayHeight; y++) {
			memset(image[y], 0, kDisplayWidth * 3);
		}
	}

	// Or if yes Sample...
	else {

		SampleRecorder* recorder = getCurrentAudioClip()->recorder;

		int64_t xScrollSamples;
		int64_t xZoomSamples;

		getCurrentAudioClip()->getScrollAndZoomInSamples(
		    currentSong->xScroll[NAVIGATION_CLIP], currentSong->xZoom[NAVIGATION_CLIP], &xScrollSamples, &xZoomSamples);

		RGB rgb = getCurrentAudioClip()->getColour();

		int32_t visibleWaveformXEnd = endSquareDisplay + 1;
		if (endMarkerVisible && blinkOn) {
			visibleWaveformXEnd--;
		}
		int32_t xEnd = std::min(kDisplayWidth, visibleWaveformXEnd);

		bool success = waveformRenderer.renderFullScreen(getSample(), xScrollSamples, xZoomSamples, image,
		                                                 &getCurrentAudioClip()->renderData, recorder, rgb,
		                                                 getCurrentAudioClip()->sampleControls.reversed, xEnd);

		// If card being accessed and waveform would have to be re-examined, come back later
		if (!success && image == PadLEDs::image) {
			uiNeedsRendering(this, whichRows, 0);
			return true;
		}
	}

	if (drawUndefinedArea) {

		for (int32_t y = 0; y < kDisplayHeight; y++) {

			if (endSquareDisplay < kDisplayWidth) {

				if (endSquareDisplay >= 0) {
					if (endMarkerVisible && blinkOn) {
						image[y][endSquareDisplay][0] = 255;
						image[y][endSquareDisplay][1] = 0;
						image[y][endSquareDisplay][2] = 0;
					}
				}

				int32_t xDisplay = endSquareDisplay + 1;

				if (xDisplay >= kDisplayWidth) {
					continue;
				}
				else if (xDisplay < 0) {
					xDisplay = 0;
				}

				std::fill(&image[y][xDisplay], &image[y][xDisplay] + (kDisplayWidth - xDisplay), colours::grey);
			}
		}
	}

	return true;
}

ActionResult AudioClipView::timerCallback() {
	blinkOn = !blinkOn;
	uiNeedsRendering(this, 0xFFFFFFFF, 0); // Very inefficient!

	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kSampleMarkerBlinkTime);

	return ActionResult::DEALT_WITH;
}

bool AudioClipView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                  uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING)) {
		return true;
	}

	for (int32_t y = 0; y < kDisplayHeight; y++) {
		RGB* const start = &image[y][kDisplayWidth];
		std::fill(start, start + kSideBarWidth, colours::black);
	}

	return true;
}

const uint8_t zeroes[] = {0, 0, 0, 0, 0, 0, 0, 0};
const uint8_t twos[] = {2, 2, 2, 2, 2, 2, 2, 2};

void AudioClipView::graphicsRoutine() {

	if (isUIModeActive(UI_MODE_AUDIO_CLIP_COLLAPSING)) {
		return;
	}

	int32_t newTickSquare;

	if (!playbackHandler.playbackState || !currentSong->isClipActive(getCurrentClip())
	    || currentUIMode == UI_MODE_EXPLODE_ANIMATION || playbackHandler.ticksLeftInCountIn) {
		newTickSquare = 255;
	}

	// Tempoless or arranger recording
	else if (!playbackHandler.isEitherClockActive()
	         || (currentPlaybackMode == &arrangement && getCurrentClip()->getCurrentlyRecordingLinearly())) {
		newTickSquare = kDisplayWidth - 1;

		// Linearly recording
		if (getCurrentClip()->getCurrentlyRecordingLinearly()) { // This would have to be true if we got here, I think?
			getCurrentAudioClip()->renderData.xScroll = -1;      // Make sure values are recalculated
			needsRenderingDependingOnSubMode();
		}
	}

	else {
		newTickSquare = getTickSquare();

		if (getCurrentAudioClip()->getCurrentlyRecordingLinearly()) {
			needsRenderingDependingOnSubMode();
		}

		if (newTickSquare < 0 || newTickSquare >= kDisplayWidth) {
			newTickSquare = 255;
		}
	}

	if (PadLEDs::flashCursor != FLASH_CURSOR_OFF && (newTickSquare != lastTickSquare || mustRedrawTickSquares)) {

		uint8_t tickSquares[kDisplayHeight];
		memset(tickSquares, newTickSquare, kDisplayHeight);

		const uint8_t* colours = getCurrentClip()->getCurrentlyRecordingLinearly() ? twos : zeroes;
		PadLEDs::setTickSquares(tickSquares, colours);

		lastTickSquare = newTickSquare;

		mustRedrawTickSquares = false;
	}
}

void AudioClipView::needsRenderingDependingOnSubMode() {

	switch (currentUIMode) {
	case UI_MODE_HORIZONTAL_SCROLL:
	case UI_MODE_HORIZONTAL_ZOOM:
		break;

	default:
		uiNeedsRendering(this, 0xFFFFFFFF, 0);
	}
}

ActionResult AudioClipView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	ActionResult result;

	// Song view button
	if (b == SESSION_VIEW) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);

			if (currentSong->lastClipInstanceEnteredStartPos != -1 || getCurrentClip()->isArrangementOnlyClip()) {
				bool success = arrangerView.transitionToArrangementEditor();
				if (!success) {
					goto doOther;
				}
			}
			else {
doOther:
				sessionView.transitionToSessionView();
			}
		}
	}

	// Clip view button
	else if (b == CLIP_VIEW) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			changeRootUI(&automationView);
		}
	}

	else if (b == PLAY) {
dontDeactivateMarker:
		return ClipView::buttonAction(b, on, inCardRoutine);
	}

	else if (b == RECORD) {
		goto dontDeactivateMarker;
	}

	else if (b == SHIFT) {
		goto dontDeactivateMarker;
	}

	else if (b == X_ENC) {
		goto dontDeactivateMarker;
	}

	// Select button, without shift
	else if (b == SELECT_ENC && !Buttons::isShiftButtonPressed()) {
		if (on && currentUIMode == UI_MODE_NONE) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			if (!soundEditor.setup(getCurrentClip())) {
				return ActionResult::DEALT_WITH;
			}
			openUI(&soundEditor);

			result = ActionResult::DEALT_WITH;
			goto deactivateMarkerIfNecessary;
		}
	}

	// Back button to clear Clip
	else if (b == BACK && currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			// Clear Clip
			Action* action = actionLogger.getNewAction(ActionType::CLIP_CLEAR, ActionAddition::NOT_ALLOWED);

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack =
			    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, getCurrentClip());

			getCurrentAudioClip()->clear(action, modelStack);
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_AUDIO_CLIP_CLEARED));
			endMarkerVisible = false;
			uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
			uiNeedsRendering(this, 0xFFFFFFFF, 0);
		}
	}
	else {

		result = ClipMinder::buttonAction(b, on);
		if (result == ActionResult::NOT_DEALT_WITH) {
			result = ClipView::buttonAction(b, on, inCardRoutine);
		}

		if (result != ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
deactivateMarkerIfNecessary:
			if (endMarkerVisible) {
				endMarkerVisible = false;
				if (getCurrentUI() == this) {
					uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
				}
				uiNeedsRendering(this, 0xFFFFFFFF, 0);
			}
		}

		return result;
	}

	return ActionResult::DEALT_WITH;
}

ActionResult AudioClipView::padAction(int32_t x, int32_t y, int32_t on) {

	// Edit pad action...
	if (x < kDisplayWidth) {

		if (Buttons::isButtonPressed(deluge::hid::button::TEMPO_ENC)) {
			if (on) {
				playbackHandler.grabTempoFromClip(getCurrentAudioClip());
			}
		}

		else {

			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			// Maybe go to SoundEditor
			ActionResult soundEditorResult = soundEditor.potentialShortcutPadAction(x, y, on);
			if (soundEditorResult != ActionResult::NOT_DEALT_WITH) {

				if (soundEditorResult == ActionResult::DEALT_WITH) {
					endMarkerVisible = false;
					uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
					uiNeedsRendering(this, 0xFFFFFFFF, 0);
				}

				return soundEditorResult;
			}

			else if (on && !currentUIMode) {

				AudioClip* clip = getCurrentAudioClip();

				int32_t endSquareDisplay = divide_round_negative(
				    clip->loopLength - currentSong->xScroll[NAVIGATION_CLIP] - 1,
				    currentSong->xZoom[NAVIGATION_CLIP]); // Rounds it well down, so we get the "final square" kinda...

				// Marker already visible
				if (endMarkerVisible) {

					// If tapped on the marker itself again, make it invisible
					if (x == endSquareDisplay) {
						if (blinkOn) {
							uiNeedsRendering(this, 0xFFFFFFFF, 0);
						}
						uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
						endMarkerVisible = false;
					}

					// Otherwise, move it
					else {

						Sample* sample = getSample();
						if (sample) {

							int32_t oldLength = clip->loopLength;

							// Ok, move the marker!
							int32_t newLength =
							    (x + 1) * currentSong->xZoom[NAVIGATION_CLIP] + currentSong->xScroll[NAVIGATION_CLIP];

							uint64_t oldLengthSamples = clip->sampleHolder.getDurationInSamples(true);

							uint64_t newLengthSamples = (uint64_t)(oldLengthSamples * newLength + (oldLength >> 1))
							                            / (uint32_t)oldLength; // Rounded

							uint64_t* valueToChange;
							int64_t newEndPosSamples;

							// AudioClip reversed
							if (clip->sampleControls.reversed) {

								newEndPosSamples = clip->sampleHolder.getEndPos(true) - newLengthSamples;

								// If the end pos is very close to the end pos marked in the audio file, assume some
								// rounding happened along the way and just go with the original
								if (sample->fileLoopStartSamples) {
									int64_t distanceFromFileEndMarker =
									    newEndPosSamples - (uint64_t)sample->fileLoopStartSamples;
									if (distanceFromFileEndMarker < 0) {
										distanceFromFileEndMarker = -distanceFromFileEndMarker; // abs
									}
									if (distanceFromFileEndMarker < 10) {
										newEndPosSamples = sample->fileLoopStartSamples;
										goto setTheStartPos;
									}
								}

								// Or if very close to actual wave start...
								{
									int64_t distanceFromFileEndMarker = newEndPosSamples;
									if (distanceFromFileEndMarker < 0) {
										distanceFromFileEndMarker = -distanceFromFileEndMarker; // abs
									}
									if (distanceFromFileEndMarker < 10) {
										newEndPosSamples = 0;
									}
								}

								// If end pos less than 0, not allowed
								if (newEndPosSamples < 0) {
									return ActionResult::DEALT_WITH;
								}
setTheStartPos:
								valueToChange = &clip->sampleHolder.startPos;
							}

							// AudioClip playing forward
							else {
								newEndPosSamples = clip->sampleHolder.startPos + newLengthSamples;

								// If the end pos is very close to the end pos marked in the audio file, assume some
								// rounding happened along the way and just go with the original
								if (sample->fileLoopEndSamples) {
									int64_t distanceFromFileEndMarker =
									    newEndPosSamples - (uint64_t)sample->fileLoopEndSamples;
									if (distanceFromFileEndMarker < 0) {
										distanceFromFileEndMarker = -distanceFromFileEndMarker; // abs
									}
									if (distanceFromFileEndMarker < 10) {
										newEndPosSamples = sample->fileLoopEndSamples;
										goto setTheEndPos;
									}
								}

								// Or if very close to actual wave length...
								{
									int64_t distanceFromWaveformEnd =
									    newEndPosSamples - (uint64_t)sample->lengthInSamples;
									if (distanceFromWaveformEnd < 0) {
										distanceFromWaveformEnd = -distanceFromWaveformEnd; // abs
									}
									if (distanceFromWaveformEnd < 10) {
										newEndPosSamples = sample->lengthInSamples;
									}
								}
setTheEndPos:
								valueToChange = &clip->sampleHolder.endPos;
							}

							ActionType actionType = (newLength < oldLength) ? ActionType::CLIP_LENGTH_DECREASE
							                                                : ActionType::CLIP_LENGTH_INCREASE;

							// Change sample end-pos value. Must do this before calling setClipLength(), which will end
							// up reading this value.
							uint64_t oldValue = *valueToChange;
							*valueToChange = newEndPosSamples;

							Action* action = actionLogger.getNewAction(actionType, ActionAddition::NOT_ALLOWED);
							currentSong->setClipLength(clip, newLength, action);

							if (action) {
								if (action->firstConsequence
								    && action->firstConsequence->type == Consequence::CLIP_LENGTH) {
									ConsequenceClipLength* consequence =
									    (ConsequenceClipLength*)action->firstConsequence;
									consequence->pointerToMarkerValue = valueToChange;
									consequence->markerValueToRevertTo = oldValue;
								}
								actionLogger.closeAction(actionType);
							}

							goto needRendering;
						}
					}
				}

				// Or, marker not already visible
				else {
					if (x == endSquareDisplay || x == endSquareDisplay + 1) {
						endMarkerVisible = true;
needRendering:
						uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kSampleMarkerBlinkTime);
						blinkOn = true;
						uiNeedsRendering(this, 0xFFFFFFFF, 0);
					}
				}
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

void AudioClipView::playbackEnded() {

	// A few reasons we might want to redraw the waveform. If a Sample had only partially recorded, it will have just
	// been discarded. Or, if tempoless or arrangement recording, zoom and everything will have just changed
	uiNeedsRendering(this, 0xFFFFFFFF, 0);
}

void AudioClipView::clipNeedsReRendering(Clip* clip) {
	if (clip == getCurrentAudioClip()) {

		// Scroll back left if we need to - it's possible that the length just reverted, if recording got aborted.
		// Ok, coming back to this, it seems it was a bit hacky that I put this in this function...
		if (currentSong->xScroll[NAVIGATION_CLIP] >= clip->loopLength) {
			horizontalScrollForLinearRecording(0);
		}
		else {
			uiNeedsRendering(this, 0xFFFFFFFF, 0);
		}
	}
}

void AudioClipView::sampleNeedsReRendering(Sample* sample) {
	if (sample == getSample()) {
		uiNeedsRendering(this, 0xFFFFFFFF, 0);
	}
}

void AudioClipView::selectEncoderAction(int8_t offset) {
	if (currentUIMode) {
		return;
	}

	view.navigateThroughAudioOutputsForAudioClip(offset, getCurrentAudioClip());
}

ActionResult AudioClipView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (!currentUIMode && Buttons::isShiftButtonPressed() && !Buttons::isButtonPressed(deluge::hid::button::Y_ENC)) {
		if (inCardRoutine && !allowSomeUserActionsEvenWhenInCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; // Allow sometimes.
		}

		// Shift colour spectrum
		getCurrentAudioClip()->colourOffset += offset;
		uiNeedsRendering(this, 0xFFFFFFFF, 0);
	}
	return ActionResult::DEALT_WITH;
}

bool AudioClipView::setupScroll(uint32_t oldScroll) {

	if (!getCurrentAudioClip()->currentlyScrollableAndZoomable()) {
		return false;
	}

	return ClipView::setupScroll(oldScroll);
}

void AudioClipView::tellMatrixDriverWhichRowsContainSomethingZoomable() {
	memset(PadLEDs::transitionTakingPlaceOnRow, 1, sizeof(PadLEDs::transitionTakingPlaceOnRow));
}

uint32_t AudioClipView::getMaxLength() {
	if (endMarkerVisible) {
		return getCurrentClip()->loopLength + 1;
	}
	else {
		return getCurrentClip()->loopLength;
	}
}

uint32_t AudioClipView::getMaxZoom() {
	int32_t maxZoom = getCurrentClip()->getMaxZoom();
	if (endMarkerVisible && maxZoom < 1073741824) {
		maxZoom <<= 1;
	}
	return maxZoom;
}
