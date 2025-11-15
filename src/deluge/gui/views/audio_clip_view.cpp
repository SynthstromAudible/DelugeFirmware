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
#include "deluge/model/settings/runtime_feature_settings.h"
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
#include "model/sample/sample_playback_guide.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/playback_mode.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/flash_storage.h"

extern "C" {
extern uint8_t currentlyAccessingCard;
}

using namespace deluge::gui;

PLACE_SDRAM_BSS AudioClipView audioClipView{};

inline Sample* getSample() {
	AudioClip& clip = *getCurrentAudioClip();
	if (clip.getCurrentlyRecordingLinearly()) {
		return clip.recorder->sample;
	}
	return static_cast<Sample*>(clip.sampleHolder.audioFile);
}

PLACE_SDRAM_TEXT bool AudioClipView::opened() {
	mustRedrawTickSquares = true;
	uiNeedsRendering(this);

	getCurrentClip()->onAutomationClipView = false;

	focusRegained();
	return true;
}

PLACE_SDRAM_TEXT void AudioClipView::focusRegained() {
	ClipView::focusRegained();
	endMarkerVisible = false;
	startMarkerVisible = false;
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

PLACE_SDRAM_TEXT void AudioClipView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	view.displayOutputName(getCurrentOutput(), false, getCurrentClip());
}

PLACE_SDRAM_TEXT bool AudioClipView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                    bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING) || isUIModeActive(UI_MODE_IMPLODE_ANIMATION)) {
		return true;
	}

	// If no Sample, just clear display
	if (!getSample()) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			memset(image[y], 0, kDisplayWidth * 3);
		}
		return true;
	}

	// If no audio clip, clear display
	AudioClip* clipPtr = getCurrentAudioClip();
	if (!clipPtr) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			memset(image[y], 0, kDisplayWidth * 3);
		}
		return true;
	}

	AudioClip& clip = *clipPtr;
	SampleRecorder* recorder = clip.recorder;

	// end marker column
	int32_t endSquareDisplay = divide_round_negative(clip.loopLength - currentSong->xScroll[NAVIGATION_CLIP] - 1,
	                                                 currentSong->xZoom[NAVIGATION_CLIP]);

	// start marker column
	int32_t startSquareDisplay =
	    divide_round_negative(0 - currentSong->xScroll[NAVIGATION_CLIP], currentSong->xZoom[NAVIGATION_CLIP]);

	int64_t xScrollSamples;
	int64_t xZoomSamples;
	clip.getScrollAndZoomInSamples(currentSong->xScroll[NAVIGATION_CLIP], currentSong->xZoom[NAVIGATION_CLIP],
	                               &xScrollSamples, &xZoomSamples);

	RGB rgb = clip.getColour();

	// Adjust xEnd if end marker is blinking
	int32_t visibleWaveformXEnd = endSquareDisplay + 1;
	if (endMarkerVisible && blinkOn) {
		visibleWaveformXEnd--;
	}
	int32_t xEnd = std::min(kDisplayWidth, visibleWaveformXEnd);

	bool success = waveformRenderer.renderFullScreen(getSample(), xScrollSamples, xZoomSamples, image, &clip.renderData,
	                                                 recorder, rgb, clip.sampleControls.isCurrentlyReversed(), xEnd);

	// If card being accessed and waveform would have to be re-examined, come back later
	if (!success && image == PadLEDs::image) {
		uiNeedsRendering(this, whichRows, 0);
		return true;
	}

	// If asked, draw grey regions + flashing columns
	if (drawUndefinedArea) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {

			// -------- END marker ----------
			if (endSquareDisplay < kDisplayWidth) {
				if (endSquareDisplay >= 0) {
					// If endMarkerVisible, show red (bright vs. dim).
					if (endMarkerVisible) {
						if (blinkOn) {
							image[y][endSquareDisplay] = colours::red;
						}
						else {
							image[y][endSquareDisplay] = colours::red_dull;
						}
					}
				}
				int32_t xDisplay = endSquareDisplay + 1;
				if (xDisplay < kDisplayWidth) {
					if (xDisplay < 0) {
						xDisplay = 0;
					}
					RGB greyCol = colours::grey;
					std::fill(&image[y][xDisplay], &image[y][kDisplayWidth], greyCol);
				}
			}

			// -------- START marker ----------

			if (startSquareDisplay >= 0) {
				if (startSquareDisplay < kDisplayWidth) {
					// Fill grey area first
					// int32_t fillEnd = startSquareDisplay;
					// if (fillEnd > kDisplayWidth) {
					//     fillEnd = kDisplayWidth;
					// }
					// for (int32_t xPos = 0; xPos < fillEnd; ++xPos) {
					//     image[y][xPos][0] = colours::grey;
					// }

					// Then overlay the green start marker if visible
					if (startMarkerVisible) {
						if (blinkOn) {
							// bright green
							image[y][startSquareDisplay] = colours::green;
						}
						else {
							// dim green - using a darker version of green
							image[y][startSquareDisplay] = colours::green.dim();
						}
					}
					// else {
					//     // If not visible, ensure this column is grey
					//     image[y][startSquareDisplay] = colours::grey;
					// }
				}
				else {
					RGB greyCol = colours::grey;
					std::fill(&image[y][0], &image[y][kDisplayWidth], greyCol);
				}
			}
		}
	}

	return true;
}

PLACE_SDRAM_TEXT ActionResult AudioClipView::timerCallback() {
	blinkOn = !blinkOn;
	uiNeedsRendering(this, 0xFFFFFFFF, 0); // Very inefficient!

	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kSampleMarkerBlinkTime);
	return ActionResult::DEALT_WITH;
}

PLACE_SDRAM_TEXT bool AudioClipView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	if (isUIModeActive(UI_MODE_INSTRUMENT_CLIP_COLLAPSING) || isUIModeActive(UI_MODE_IMPLODE_ANIMATION)) {
		return true;
	}

	int32_t macroColumn = kDisplayWidth;
	bool armed = false;
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		RGB* const start = &image[y][kDisplayWidth];
		std::fill(start, start + kSideBarWidth, colours::black);

		if (isUIModeActive(UI_MODE_HOLDING_SONG_BUTTON)) {
			armed |= view.renderMacros(macroColumn, y, -1, image, occupancyMask);
		}
	}
	if (armed) {
		view.flashPlayEnable();
	}

	return true;
}

PLACE_SDRAM_TEXT void AudioClipView::graphicsRoutine() {
	if (isUIModeActive(UI_MODE_AUDIO_CLIP_COLLAPSING)) {
		return;
	}

	int32_t newTickSquare;

	if (!playbackHandler.playbackState || !currentSong->isClipActive(getCurrentClip())
	    || currentUIMode == UI_MODE_EXPLODE_ANIMATION || currentUIMode == UI_MODE_IMPLODE_ANIMATION
	    || playbackHandler.ticksLeftInCountIn) {
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

		std::array<uint8_t, 8> coloursArray = {0};
		if (getCurrentClip()->getCurrentlyRecordingLinearly()) {
			coloursArray.fill(2);
		}

		PadLEDs::setTickSquares(tickSquares, coloursArray.data());

		lastTickSquare = newTickSquare;
		mustRedrawTickSquares = false;
	}
}

PLACE_SDRAM_TEXT void AudioClipView::needsRenderingDependingOnSubMode() {
	switch (currentUIMode) {
	case UI_MODE_HORIZONTAL_SCROLL:
	case UI_MODE_HORIZONTAL_ZOOM:
		break;
	default:
		uiNeedsRendering(this, 0xFFFFFFFF, 0);
	}
}

// If you want your specialized button logic (session view, clip view, etc.),
// put that here. Otherwise call the parent:
PLACE_SDRAM_TEXT ActionResult AudioClipView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	ActionResult result;

	// Song view button
	if (b == SESSION_VIEW) {
		if (on) {
			if (currentUIMode == UI_MODE_NONE) {
				currentUIMode = UI_MODE_HOLDING_SONG_BUTTON;
				timeSongButtonPressed = AudioEngine::audioSampleTimer;
				indicator_leds::setLedState(IndicatorLED::SESSION_VIEW, true);
				uiNeedsRendering(this, 0, 0xFFFFFFFF);
			}
		}
		else {
			if (!isUIModeActive(UI_MODE_HOLDING_SONG_BUTTON)) {
				return ActionResult::DEALT_WITH;
			}
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			exitUIMode(UI_MODE_HOLDING_SONG_BUTTON);
			if ((int32_t)(AudioEngine::audioSampleTimer - timeSongButtonPressed) > kShortPressTime) {
				uiNeedsRendering(this, 0, 0xFFFFFFFF);
				indicator_leds::setLedState(IndicatorLED::SESSION_VIEW, false);
				return ActionResult::DEALT_WITH;
			}

			uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);

			ClipMinder::transitionToArrangerOrSession();
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
		// removing time stretching by re-calculating clip length based on length of audio sample
		if (Buttons::isButtonPressed(deluge::hid::button::Y_ENC)) {
			if (on && currentUIMode == UI_MODE_NONE) {
				if (inCardRoutine) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}
				setClipLengthEqualToSampleLength();
			}
		}
		// if shift is pressed then we're resizing the clip without time stretching
		else if (!Buttons::isShiftButtonPressed()) {
			goto dontDeactivateMarker;
		}
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

			getCurrentAudioClip()->clear(action, modelStack, !FlashStorage::automationClear, true);

			// New default as part of Automation Clip View Implementation
			// If this is enabled, then when you are in Audio Clip View, clearing
			// a clip will only clear the Audio Sample (automations remain intact). If this is enabled, if you want to
			// clear automations, you will enter Automation Clip View and clear the clip there. If this is enabled, the
			// message displayed on the OLED screen is adjusted to reflect the nature of what is being cleared
			if (FlashStorage::automationClear) {
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_SAMPLE_CLEARED));
			}
			else {
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIP_CLEARED));
			}
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

PLACE_SDRAM_TEXT ActionResult AudioClipView::padAction(int32_t x, int32_t y, int32_t on) {
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
					startMarkerVisible = false;
					uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
					uiNeedsRendering(this, 0xFFFFFFFF, 0);
				}
				return soundEditorResult;
			}
			else if (on && !currentUIMode) {
				AudioClip* clip = getCurrentAudioClip();
				if (!clip) {
					return ActionResult::DEALT_WITH;
				}
				AudioClip& clipRef = *clip;

				int32_t endSquareDisplay =
				    divide_round_negative(clipRef.loopLength - currentSong->xScroll[NAVIGATION_CLIP] - 1,
				                          currentSong->xZoom[NAVIGATION_CLIP]);

				int32_t startSquareDisplay = divide_round_negative(0 - currentSong->xScroll[NAVIGATION_CLIP],
				                                                   currentSong->xZoom[NAVIGATION_CLIP]);

				// =========== Handling END marker =============
				if (endMarkerVisible) {
					// If user taps the same or adjacent end marker column => toggle off
					if (x == endSquareDisplay || x == startSquareDisplay) {
						endMarkerVisible = false;
						uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
						uiNeedsRendering(this, 0xFFFFFFFF, 0);
					}
					else {
						Sample* sample = getSample();
						if (sample) {
							int32_t newLength =
							    (x + 1) * currentSong->xZoom[NAVIGATION_CLIP] + currentSong->xScroll[NAVIGATION_CLIP];
							int32_t oldLength = clipRef.loopLength;
							uint64_t oldLengthSamples = clipRef.sampleHolder.getDurationInSamples(true);
							changeUnderlyingSampleLength(clipRef, sample, newLength, oldLength, oldLengthSamples);
							uiNeedsRendering(this, 0xFFFFFFFF, 0);
						}
					}
				}
				// =========== Handling START marker =============
				else if (startMarkerVisible) {
					if (x == startSquareDisplay || x == endSquareDisplay) {
						startMarkerVisible = false; // Toggle start marker off
						uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
						uiNeedsRendering(this, 0xFFFFFFFF, 0);
					}
					else {
						Sample* sample = getSample();
						if (sample) {
							int32_t newStartTicks =
							    x * currentSong->xZoom[NAVIGATION_CLIP] + currentSong->xScroll[NAVIGATION_CLIP];
							int32_t oldLength = clipRef.loopLength;
							uint64_t oldLengthSamples = clipRef.sampleHolder.getDurationInSamples(true);
							changeUnderlyingSampleStart(clipRef, sample, newStartTicks, oldLength, oldLengthSamples);
							uiNeedsRendering(this, 0xFFFFFFFF, 0);
						}
					}
				}
				else {
					// No marker is visible. Are we near the end or start?
					if (x == endSquareDisplay || x == endSquareDisplay + 1) {
						endMarkerVisible = true;
						startMarkerVisible = false;
						blinkOn = true;
						uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kSampleMarkerBlinkTime);
						uiNeedsRendering(this, 0xFFFFFFFF, 0);
					}
					else if (x == startSquareDisplay) {

						// WIP: Allow the user to trim from the start of the audio clip
						if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::TrimFromStartOfAudioClip)) {
							startMarkerVisible = true;
							endMarkerVisible = false;
							blinkOn = true;
							uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kSampleMarkerBlinkTime);
							uiNeedsRendering(this, 0xFFFFFFFF, 0);
						}
					}
				}
			}
		}
	}
	else if (x == kDisplayWidth) {
		if (isUIModeActive(UI_MODE_HOLDING_SONG_BUTTON)) {
			if (sdRoutineLock) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			if (!on) {
				view.activateMacro(y);
			}
			return ActionResult::DEALT_WITH;
		}
	}
	return ActionResult::DEALT_WITH;
}

// ----------- "End" pointer logic -----------
PLACE_SDRAM_TEXT void AudioClipView::changeUnderlyingSampleLength(AudioClip& clip, const Sample* sample,
                                                                  int32_t newLength, int32_t oldLength,
                                                                  uint64_t oldLengthSamples) const {
	uint64_t* valueToChange;
	int64_t newEndPosSamples;

	uint64_t newLengthSamples =
	    (uint64_t)(oldLengthSamples * (uint64_t)newLength + (oldLength >> 1)) / (uint32_t)oldLength;

	// If end pos less than 0, not allowed
	if (clip.sampleControls.isCurrentlyReversed()) {
		newEndPosSamples = clip.sampleHolder.endPos - newLengthSamples;
		if (newEndPosSamples < 0) {
			newEndPosSamples = 0;
		}
		valueToChange = &clip.sampleHolder.startPos;
	}
	// AudioClip playing forward
	else {
		newEndPosSamples = clip.sampleHolder.startPos + newLengthSamples;
		if (newEndPosSamples > sample->lengthInSamples) {
			newEndPosSamples = sample->lengthInSamples;
		}
		valueToChange = &clip.sampleHolder.endPos;

		// If the end pos is very close to the end pos marked in the audio file...
		if (sample->fileLoopStartSamples) {
			int64_t distanceFromFileEndMarker = newEndPosSamples - (uint64_t)sample->fileLoopStartSamples;
			if (distanceFromFileEndMarker < 0) {
				distanceFromFileEndMarker = -distanceFromFileEndMarker;
			}
			if (distanceFromFileEndMarker < 10) {
				newEndPosSamples = sample->fileLoopStartSamples;
			}
		}
	}

	ActionType actionType =
	    (newLength < oldLength) ? ActionType::CLIP_LENGTH_DECREASE : ActionType::CLIP_LENGTH_INCREASE;

	// Change sample end-pos value. Must do this before calling setClipLength(), which will end
	// up reading this value.
	uint64_t oldValue = *valueToChange;
	*valueToChange = newEndPosSamples;

	Action* action = actionLogger.getNewAction(actionType, ActionAddition::NOT_ALLOWED);
	currentSong->setClipLength(&clip, newLength, action);
	if (action) {
		if (action->firstConsequence && action->firstConsequence->type == Consequence::CLIP_LENGTH) {
			ConsequenceClipLength* consequence = (ConsequenceClipLength*)action->firstConsequence;
			consequence->pointerToMarkerValue = valueToChange;
			consequence->markerValueToRevertTo = oldValue;
		}
		actionLogger.closeAction(actionType);
	}
}

// ----------- "Start" pointer logic -----------
PLACE_SDRAM_TEXT void AudioClipView::changeUnderlyingSampleStart(AudioClip& clip, const Sample* sample,
                                                                 int32_t newStartTicks, int32_t oldLength,
                                                                 uint64_t oldLengthSamples) const {
	int32_t oldEndTick = oldLength;
	int32_t newLengthTicks = oldEndTick - newStartTicks;
	if (newLengthTicks < 1) {
		newLengthTicks = 1;
	}
	uint64_t newLengthSamples =
	    static_cast<uint64_t>(oldLengthSamples * newLengthTicks + (oldLength / 2)) / static_cast<uint32_t>(oldLength);

	if (clip.sampleControls.isCurrentlyReversed()) {
		uint64_t oldValue = clip.sampleHolder.endPos;
		uint64_t newEndPos = clip.sampleHolder.startPos + newLengthSamples;
		if (newEndPos > sample->lengthInSamples) {
			newEndPos = sample->lengthInSamples;
		}
		clip.sampleHolder.endPos = newEndPos;

		ActionType actionType =
		    (newLengthTicks < oldLength) ? ActionType::CLIP_LENGTH_DECREASE : ActionType::CLIP_LENGTH_INCREASE;
		Action* action = actionLogger.getNewAction(actionType, ActionAddition::NOT_ALLOWED);
		currentSong->setClipLength(&clip, newLengthTicks, action);
		if (action) {
			if (action->firstConsequence && action->firstConsequence->type == Consequence::CLIP_LENGTH) {
				ConsequenceClipLength* consequence = (ConsequenceClipLength*)action->firstConsequence;
				consequence->pointerToMarkerValue = &clip.sampleHolder.endPos;
				consequence->markerValueToRevertTo = oldValue;
			}
			actionLogger.closeAction(actionType);
		}
	}
	else {
		uint64_t oldValue = clip.sampleHolder.startPos;
		uint64_t newStartPos = clip.sampleHolder.endPos - newLengthSamples;
		if ((int64_t)newStartPos < 0) {
			newStartPos = 0;
		}
		clip.sampleHolder.startPos = newStartPos;

		ActionType actionType =
		    (newLengthTicks < oldLength) ? ActionType::CLIP_LENGTH_DECREASE : ActionType::CLIP_LENGTH_INCREASE;
		Action* action = actionLogger.getNewAction(actionType, ActionAddition::NOT_ALLOWED);
		currentSong->setClipLength(&clip, newLengthTicks, action);
		if (action) {
			if (action->firstConsequence && action->firstConsequence->type == Consequence::CLIP_LENGTH) {
				ConsequenceClipLength* consequence = (ConsequenceClipLength*)action->firstConsequence;
				consequence->pointerToMarkerValue = &clip.sampleHolder.startPos;
				consequence->markerValueToRevertTo = oldValue;
			}
			actionLogger.closeAction(actionType);
		}
	}
}

PLACE_SDRAM_TEXT void AudioClipView::playbackEnded() {
	uiNeedsRendering(this, 0xFFFFFFFF, 0);
}

PLACE_SDRAM_TEXT void AudioClipView::clipNeedsReRendering(Clip* c) {
	if (c == getCurrentAudioClip()) {
		// Scroll back left if we need to - it's possible that the length just reverted, if recording got aborted.
		// Ok, coming back to this, it seems it was a bit hacky that I put this in this function...
		if (currentSong->xScroll[NAVIGATION_CLIP] >= c->loopLength) {
			horizontalScrollForLinearRecording(0);
		}
		else {
			uiNeedsRendering(this, 0xFFFFFFFF, 0);
		}
	}
}

PLACE_SDRAM_TEXT void AudioClipView::sampleNeedsReRendering(Sample* s) {
	if (s == getSample()) {
		uiNeedsRendering(this, 0xFFFFFFFF, 0);
	}
}

PLACE_SDRAM_TEXT void AudioClipView::selectEncoderAction(int8_t offset) {
	if (currentUIMode) {
		return;
	}
	auto ao = (AudioOutput*)getCurrentAudioClip()->output;
	ao->scrollAudioOutputMode(offset);
}

PLACE_SDRAM_TEXT void AudioClipView::setClipLengthEqualToSampleLength() {
	AudioClip& audioClip = *getCurrentAudioClip();
	SamplePlaybackGuide guide = audioClip.guide;
	SampleHolder* sampleHolder = (SampleHolder*)guide.audioFileHolder;
	if (sampleHolder) {
		adjustLoopLength(sampleHolder->getLoopLengthAtSystemSampleRate(true));
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIP_LENGTH_ADJUSTED));
	}
	else {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_SAMPLE));
	}
}

PLACE_SDRAM_TEXT void AudioClipView::adjustLoopLength(int32_t newLength) {
	int32_t oldLength = getCurrentClip()->loopLength;

	if (oldLength != newLength) {
		Action* action = nullptr;

		if (newLength > oldLength) {
			// If we're still within limits
			if (newLength <= (uint32_t)kMaxSequenceLength) {
				action = lengthenClip(newLength);
doReRender:
				// use getRootUI() in case this is called from audio clip automation view
				uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0);
			}
		}
		else if (newLength < oldLength) {
			if (newLength > 0) {
				action = shortenClip(newLength);
				// Scroll / zoom as needed
				if (!scrollLeftIfTooFarRight(newLength)) {
					if (!zoomToMax(true)) {
						goto doReRender;
					}
				}
			}
		}

		displayNumberOfBarsAndBeats(newLength, currentSong->xZoom[NAVIGATION_CLIP], false, "LONG");
		if (action) {
			action->xScrollClip[AFTER] = currentSong->xScroll[NAVIGATION_CLIP];
		}
	}
}

PLACE_SDRAM_TEXT ActionResult AudioClipView::horizontalEncoderAction(int32_t offset) {
	// Shift and x pressed - edit length of clip without timestretching
	if (isNoUIModeActive() && Buttons::isButtonPressed(deluge::hid::button::X_ENC) && Buttons::isShiftButtonPressed()) {
		return editClipLengthWithoutTimestretching(offset);
	}
	else {
		// Otherwise, let parent do scrolling and zooming
		return ClipView::horizontalEncoderAction(offset);
	}
}

PLACE_SDRAM_TEXT ActionResult AudioClipView::editClipLengthWithoutTimestretching(int32_t offset) {
	// If tempoless recording, don't allow
	if (!getCurrentClip()->currentlyScrollableAndZoomable()) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_EDIT_LENGTH));
		return ActionResult::DEALT_WITH;
	}

	// If we're not scrolled all the way to the right, go there now
	if (scrollRightToEndOfLengthIfNecessary(getCurrentClip()->loopLength)) {
		return ActionResult::DEALT_WITH;
	}

	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	int32_t oldLength = getCurrentClip()->loopLength;
	uint64_t oldLengthSamples = getCurrentAudioClip()->sampleHolder.getDurationInSamples(true);

	Action* action = nullptr;
	uint32_t newLength = changeClipLength(offset, oldLength, action);

	AudioClip& audioClip = *getCurrentAudioClip();
	SamplePlaybackGuide guide = audioClip.guide;
	SampleHolder* sampleHolder = (SampleHolder*)guide.audioFileHolder;
	if (sampleHolder) {
		Sample* sample = static_cast<Sample*>(sampleHolder->audioFile);
		if (sample) {
			changeUnderlyingSampleLength(audioClip, sample, newLength, oldLength, oldLengthSamples);
		}
	}

	displayNumberOfBarsAndBeats(newLength, currentSong->xZoom[NAVIGATION_CLIP], false, "LONG");
	if (action) {
		action->xScrollClip[AFTER] = currentSong->xScroll[NAVIGATION_CLIP];
	}
	return ActionResult::DEALT_WITH;
}

PLACE_SDRAM_TEXT ActionResult AudioClipView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
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

PLACE_SDRAM_TEXT bool AudioClipView::setupScroll(uint32_t oldScroll) {
	if (!getCurrentAudioClip()->currentlyScrollableAndZoomable()) {
		return false;
	}
	return ClipView::setupScroll(oldScroll);
}

PLACE_SDRAM_TEXT void AudioClipView::tellMatrixDriverWhichRowsContainSomethingZoomable() {
	memset(PadLEDs::transitionTakingPlaceOnRow, 1, sizeof(PadLEDs::transitionTakingPlaceOnRow));
}

PLACE_SDRAM_TEXT uint32_t AudioClipView::getMaxLength() {
	if (endMarkerVisible) {
		return getCurrentClip()->loopLength + 1;
	}
	return getCurrentClip()->loopLength;
}

PLACE_SDRAM_TEXT uint32_t AudioClipView::getMaxZoom() {
	int32_t maxZoom = getCurrentClip()->getMaxZoom();
	if (endMarkerVisible && maxZoom < 1073741824) {
		maxZoom <<= 1;
	}
	return maxZoom;
}
