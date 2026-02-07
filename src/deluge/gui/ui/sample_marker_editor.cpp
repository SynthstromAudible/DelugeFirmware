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

#include "gui/ui/sample_marker_editor.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/waveform/waveform_basic_navigator.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/instrument/instrument.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "model/voice/voice.h"
#include "model/voice/voice_sample.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/multi_range/multisample_range.h"
#include "util/cfunctions.h"
#include "util/misc.h"

using namespace deluge::gui;

const uint8_t zeroes[] = {0, 0, 0, 0, 0, 0, 0, 0};

SampleMarkerEditor sampleMarkerEditor{};

MultisampleRange& getCurrentMultisampleRange() {
	return *static_cast<MultisampleRange*>(soundEditor.currentMultiRange);
}

SampleHolder& getCurrentSampleHolder() {
	AudioClip* current = getCurrentAudioClip();
	if (current != nullptr) {
		return current->sampleHolder;
	}

	return getCurrentMultisampleRange().sampleHolder;
}

SampleControls* getCurrentSampleControls() {
	if (getCurrentClip()->type == ClipType::AUDIO) {
		return &getCurrentAudioClip()->sampleControls;
	}

	return &soundEditor.currentSource->sampleControls;
}

bool isLoopLocked() {
	return getCurrentClip()->type != ClipType::AUDIO && getCurrentMultisampleRange().sampleHolder.loopLocked;
}

MarkerType SampleMarkerEditor::reverseRemap(MarkerType type) const {
	bool reversed = getCurrentSampleControls()->reversed;

	if (reversed) {
		switch (type) {
		case MarkerType::NOT_AVAILABLE:
			[[fallthrough]];
		case MarkerType::NONE:
			return type;
		case MarkerType::START:
			return MarkerType::END;
		case MarkerType::LOOP_START:
			return MarkerType::LOOP_END;
		case MarkerType::LOOP_END:
			return MarkerType::LOOP_START;
		case MarkerType::END:
			return MarkerType::START;
		}
	}

	return type;
}

bool SampleMarkerEditor::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*cols = 0b10;
	return true;
}

bool SampleMarkerEditor::opened() {

	if (getRootUI() == &keyboardScreen) {
		PadLEDs::skipGreyoutFade();
	}

	uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);

	waveformBasicNavigator.sample = static_cast<Sample*>(getCurrentSampleHolder().audioFile);

	if (!waveformBasicNavigator.sample) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_SAMPLE));
		return false;
	}

	waveformBasicNavigator.opened(&getCurrentSampleHolder());

	blinkPhase = 0;

	uiNeedsRendering(this, 0xFFFFFFFF, 0);

	if (display->have7SEG()) {
		displayText();
	}

	if (getRootUI() != &instrumentClipView) {
		renderingNeededRegardlessOfUI(0, 0xFFFFFFFF);
	}

	focusRegained();
	return true;
}

void SampleMarkerEditor::recordScrollAndZoom() {
	if (markerType != MarkerType::NONE) {
		auto& sampleHolder = getCurrentSampleHolder();
		sampleHolder.waveformViewScroll = waveformBasicNavigator.xScroll;
		sampleHolder.waveformViewZoom = waveformBasicNavigator.xZoom;
	}
}

void SampleMarkerEditor::writeValue(uint32_t value, MarkerType markerTypeNow) {
	if (markerTypeNow == MarkerType::NOT_AVAILABLE) {
		markerTypeNow = markerType;
	}

	ClipType clipType = getCurrentClip()->type;

	bool audioClipActive;
	if (clipType == ClipType::AUDIO) {
		audioClipActive = (playbackHandler.isEitherClockActive() && currentSong->isClipActive(getCurrentClip())
		                   && getCurrentAudioClip()->voiceSample);

		getCurrentAudioClip()->unassignVoiceSample(false);
	}

	if (markerTypeNow == MarkerType::START) {
		getCurrentSampleHolder().startPos = value;
	}
	else if (markerTypeNow == MarkerType::LOOP_START) {
		auto& multiSampleRange = getCurrentMultisampleRange();
		auto& sampleHolder = multiSampleRange.sampleHolder;
		if (sampleHolder.loopLocked) {
			uint32_t intendedLoopEndPos = value + static_cast<uint32_t>(sampleHolder.loopLength());
			if (uint64_t{intendedLoopEndPos} <= sampleHolder.endPos) {
				sampleHolder.loopStartPos = value;
				sampleHolder.loopEndPos = intendedLoopEndPos;
			}
		}
		else {
			sampleHolder.loopStartPos = value;
		}
	}
	else if (markerTypeNow == MarkerType::LOOP_END) {
		auto& multiSampleRange = getCurrentMultisampleRange();
		auto& sampleHolder = multiSampleRange.sampleHolder;
		if (sampleHolder.loopLocked) {
			int32_t intendedLoopStartPos = static_cast<int32_t>(value) - sampleHolder.loopLength();
			// pos == 0 would disable the loop, so the smallest legal start position is sample 1
			if (intendedLoopStartPos >= 1 && static_cast<uint64_t>(intendedLoopStartPos) >= sampleHolder.startPos) {
				sampleHolder.loopEndPos = value;
				sampleHolder.loopStartPos = intendedLoopStartPos;
			}
		}
		else {
			sampleHolder.loopEndPos = value;
		}
	}
	else if (markerTypeNow == MarkerType::END) {
		getCurrentSampleHolder().endPos = value;
	}

	getCurrentSampleHolder().claimClusterReasons(getCurrentSampleControls()->reversed,
	                                             CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE);

	if (clipType == ClipType::AUDIO) {
		if (audioClipActive) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			getCurrentClip()->resumePlayback(modelStack, true);
		}
	}
	else {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();
		soundEditor.currentSound->sampleZoneChanged(markerTypeNow, soundEditor.currentSourceIndex, modelStack);
		getCurrentInstrument()->beenEdited(true);
	}
}

int32_t SampleMarkerEditor::getStartColOnScreen(int32_t unscrolledPos) {
	return divide_round_negative((int32_t)(unscrolledPos - waveformBasicNavigator.xScroll),
	                             waveformBasicNavigator.xZoom);
}

int32_t SampleMarkerEditor::getEndColOnScreen(int32_t unscrolledPos) {
	return divide_round_negative((int32_t)(unscrolledPos - 1 - waveformBasicNavigator.xScroll),
	                             waveformBasicNavigator.xZoom);
}

int32_t SampleMarkerEditor::getStartPosFromCol(int32_t col) {
	return waveformBasicNavigator.xScroll + col * waveformBasicNavigator.xZoom;
}

int32_t SampleMarkerEditor::getEndPosFromCol(int32_t col) {
	return waveformBasicNavigator.xScroll + (col + 1) * waveformBasicNavigator.xZoom;
}

void SampleMarkerEditor::getColsOnScreen(MarkerColumn* cols) {
	cols[util::to_underlying(MarkerType::START)].pos = getCurrentSampleHolder().startPos;
	cols[util::to_underlying(MarkerType::START)].colOnScreen =
	    getStartColOnScreen(cols[util::to_underlying(MarkerType::START)].pos);

	if (getCurrentClip()->type != ClipType::AUDIO) {
		cols[util::to_underlying(MarkerType::LOOP_START)].pos = getCurrentMultisampleRange().sampleHolder.loopStartPos;
		cols[util::to_underlying(MarkerType::LOOP_START)].colOnScreen =
		    cols[util::to_underlying(MarkerType::LOOP_START)].pos
		        ? getStartColOnScreen(cols[util::to_underlying(MarkerType::LOOP_START)].pos)
		        : kInvalidColumn;

		cols[util::to_underlying(MarkerType::LOOP_END)].pos = getCurrentMultisampleRange().sampleHolder.loopEndPos;
		cols[util::to_underlying(MarkerType::LOOP_END)].colOnScreen =
		    cols[util::to_underlying(MarkerType::LOOP_END)].pos
		        ? getEndColOnScreen(cols[util::to_underlying(MarkerType::LOOP_END)].pos)
		        : kInvalidColumn;
	}

	else {
		cols[util::to_underlying(MarkerType::LOOP_START)].pos = 0;
		cols[util::to_underlying(MarkerType::LOOP_START)].colOnScreen = kInvalidColumn;

		cols[util::to_underlying(MarkerType::LOOP_END)].pos = 0;
		cols[util::to_underlying(MarkerType::LOOP_END)].colOnScreen = kInvalidColumn;
	}

	cols[util::to_underlying(MarkerType::END)].pos = getCurrentSampleHolder().endPos;
	cols[util::to_underlying(MarkerType::END)].colOnScreen =
	    getEndColOnScreen(cols[util::to_underlying(MarkerType::END)].pos);
}

void SampleMarkerEditor::selectEncoderAction(int8_t offset) {
	if (currentUIMode && currentUIMode != UI_MODE_AUDITIONING) {
		return;
	}

	MarkerColumn cols[kNumMarkerTypes];
	getColsOnScreen(cols);

	int32_t oldCol = cols[util::to_underlying(markerType)].colOnScreen;
	int32_t oldPos = cols[util::to_underlying(markerType)].pos;
	int32_t newCol = oldCol + offset;

	// Make sure we don't drive one marker into the other
	for (int32_t c = 0; c < kNumMarkerTypes; c++) {
		if (c == util::to_underlying(markerType)) {
			continue;
		}
		if (cols[c].colOnScreen == oldCol || cols[c].colOnScreen == newCol) {
			return;
		}
	}

	int32_t newMarkerPos = (markerType < MarkerType::LOOP_END) ? getStartPosFromCol(newCol) : getEndPosFromCol(newCol);

	if (newMarkerPos < 0) {
		newMarkerPos = 0;
	}

	if (offset >= 0) {
		if (markerType == MarkerType::END && shouldAllowExtraScrollRight()) {
			if (newMarkerPos < oldPos) {
				return;
			}
		}
		else {
			if (newMarkerPos > waveformBasicNavigator.sample->lengthInSamples) {
				newMarkerPos = waveformBasicNavigator.sample->lengthInSamples;
			}
		}
	}

	writeValue(newMarkerPos);

	// If marker was on-screen...
	if (oldCol >= 0 && oldCol < kDisplayWidth) {

		getColsOnScreen(cols);
		// It might have changed, and despite having a newCol variable above, that's only our desired value - we might
		// have run into the end of the sample
		newCol = cols[util::to_underlying(markerType)].colOnScreen;

		// But isn't anymore...
		if (newCol < 0 || newCol >= kDisplayWidth) {

			// Move scroll
			waveformBasicNavigator.xScroll += waveformBasicNavigator.xZoom * offset;

			if (waveformBasicNavigator.xScroll < 0) {
				waveformBasicNavigator.xScroll = 0; // Shouldn't happen...
			}

			recordScrollAndZoom();
		}
	}

	blinkPhase = 0;

	uiNeedsRendering(this, 0xFFFFFFFF, 0);
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		displayText();
	}
}

ActionResult SampleMarkerEditor::padAction(int32_t x, int32_t y, int32_t on) {

	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	if (currentUIMode != UI_MODE_AUDITIONING) { // Don't want to do this while auditioning - too easy to do by mistake
		ActionResult soundEditorResult = soundEditor.potentialShortcutPadAction(x, y, on);
		if (soundEditorResult != ActionResult::NOT_DEALT_WITH) {
			return soundEditorResult;
		}
	}

	// Audition pads - pass to UI beneath
	if (x == kDisplayWidth + 1) {
		if (getCurrentClip()->type == ClipType::INSTRUMENT) {
			instrumentClipView.padAction(x, y, on);
		}
		return ActionResult::DEALT_WITH;
	}

	// Mute pads
	else if (x == kDisplayWidth) {
		if (on && !currentUIMode) {
			exitUI();
		}
	}

	else {

		// Press down
		if (on) {

			if (currentUIMode && currentUIMode != UI_MODE_AUDITIONING
			    && currentUIMode != UI_MODE_HOLDING_SAMPLE_MARKER) {
				return ActionResult::DEALT_WITH;
			}

			MarkerColumn cols[kNumMarkerTypes];
			getColsOnScreen(cols);

			// See which one we pressed
			MarkerType markerPressed = MarkerType::NONE;
			for (int32_t m = 0; m < kNumMarkerTypes; m++) {
				if (cols[m].colOnScreen == x) {
					if (markerPressed != MarkerType::NONE) {
						// toggle between markers if there's two overlapping columns
						if (MarkerType{m} != markerType) {
							markerPressed = MarkerType{m};
						}
					}
					else {
						markerPressed = MarkerType{m};
					}
				}
			}

			int32_t value;

			// If already holding a marker down...
			if (currentUIMode == UI_MODE_HOLDING_SAMPLE_MARKER) {

				if (getCurrentClip()->type == ClipType::INSTRUMENT) {
					// See which one we were holding down
					MarkerType markerHeld = MarkerType::NONE;
					for (int32_t m = 0; m < kNumMarkerTypes; m++) {
						if (cols[m].colOnScreen == pressX) {
							markerHeld = MarkerType{m};
						}
					}

					// -----------------------------------------------------------
					MarkerType newMarkerType;
					int32_t newValue;

					// If start or end, add a loop point
					if (markerHeld == MarkerType::START) {

						// Unless we actually just tapped the already existing loop point
						if (x == cols[util::to_underlying(MarkerType::LOOP_START)].colOnScreen) {
							markerType = MarkerType::LOOP_START;
							value = 0;
							// Unlock the loop to avoid setting the loop end to something nonsensical
							this->loopUnlock();
							writeValue(value);
							markerType = MarkerType::START; // Switch it back
							goto doRender;
						}

						// Limit position
						if (cols[util::to_underlying(MarkerType::START)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH;
						}
						if (getCurrentMultisampleRange().sampleHolder.loopEndPos
						    && cols[util::to_underlying(MarkerType::LOOP_END)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}
						if (cols[util::to_underlying(MarkerType::END)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}

						newMarkerType = MarkerType::LOOP_START;
						newValue = getStartPosFromCol(x);

ensureNotPastSampleLength:
						// Loop start and end points are not allowed to be further right than the sample waveform length
						if (newValue >= waveformBasicNavigator.sample->lengthInSamples) {
							return ActionResult::DEALT_WITH;
						}
						markerType = newMarkerType;
						value = newValue;
					}
					else if (markerHeld == MarkerType::END) {

						// Unless we actually just tapped the already existing loop point
						if (x == cols[util::to_underlying(MarkerType::LOOP_END)].colOnScreen) {
							markerType = MarkerType::LOOP_END;
							value = 0;
							// Unlock the loop to avoid setting the loop start to something nonsensical
							this->loopUnlock();
							writeValue(value);
							markerType = MarkerType::END; // Switch it back
							goto doRender;
						}

						// Limit position
						if (cols[util::to_underlying(MarkerType::START)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH;
						}
						if (cols[util::to_underlying(MarkerType::LOOP_START)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH; // Will be a big negative number if inactive
						}
						if (cols[util::to_underlying(MarkerType::END)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}

						newMarkerType = MarkerType::LOOP_END;
						newValue = getEndPosFromCol(x);

						goto ensureNotPastSampleLength;
					}
					else if (markerHeld == MarkerType::LOOP_START && markerPressed == MarkerType::LOOP_END) {
						// Toggle loop lock
						if (getCurrentMultisampleRange().sampleHolder.loopLocked) {
							this->loopUnlock();
						}
						else {
							this->loopLock();
						}
						goto doRender;
					}

					// Or if a loop point and they pressed the end marker, remove the loop point
					else if (markerHeld == MarkerType::LOOP_START) {
						if (x == cols[util::to_underlying(MarkerType::START)].colOnScreen) {
							value = 0;

							// Unlock the loop so we don't set the end position to something nonsensical.
							this->loopUnlock();
							writeValue(value);
							markerType = MarkerType::START;

exitAfterRemovingLoopMarker:
							currentUIMode = UI_MODE_NONE;
							blinkPhase = 1;
							goto doRender;
						}
						else {
							return ActionResult::DEALT_WITH;
						}
					}
					else if (markerHeld == MarkerType::LOOP_END) {
						if (x == cols[util::to_underlying(MarkerType::END)].colOnScreen) {
							value = 0;
							// Unlock the loop so we don't set the start position to something nonsensical.
							this->loopUnlock();
							writeValue(value);
							markerType = MarkerType::END;

							goto exitAfterRemovingLoopMarker;
						}
						else {
							return ActionResult::DEALT_WITH;
						}
					}

					currentUIMode = UI_MODE_NONE;
					blinkPhase = 0;
					goto doWriteValue;
				}
			}

			// Or if user not already holding a marker down...
			else {

				// If we tapped a marker...
				if (markerPressed >= MarkerType::START) {
					blinkPhase = (markerType == markerPressed) ? 0 : 1;
					markerType = markerPressed;
					currentUIMode = UI_MODE_HOLDING_SAMPLE_MARKER;
					pressX = x;
					pressY = y;
				}

				// Otherwise, move the current marker to where we tapped
				else {

					// Make sure it doesn't go past any other markers it shouldn't
					if (markerType == MarkerType::START) {
						if (cols[util::to_underlying(MarkerType::LOOP_START)].pos
						    && cols[util::to_underlying(MarkerType::LOOP_START)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}
						if (cols[util::to_underlying(MarkerType::LOOP_END)].pos
						    && cols[util::to_underlying(MarkerType::LOOP_END)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}
						if (cols[util::to_underlying(MarkerType::END)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}
					}

					else if (markerType == MarkerType::LOOP_START) {
						if (cols[util::to_underlying(MarkerType::START)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH;
						}
						if (cols[util::to_underlying(MarkerType::LOOP_END)].pos
						    && cols[util::to_underlying(MarkerType::LOOP_END)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}
						if (cols[util::to_underlying(MarkerType::END)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}
					}

					else if (markerType == MarkerType::LOOP_END) {
						if (cols[util::to_underlying(MarkerType::START)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH;
						}
						if (cols[util::to_underlying(MarkerType::LOOP_START)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH; // Will be a big negative number if inactive
						}
						if (cols[util::to_underlying(MarkerType::END)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}
					}

					else if (markerType == MarkerType::END) {
						if (cols[util::to_underlying(MarkerType::START)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH;
						}
						if (cols[util::to_underlying(MarkerType::LOOP_START)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH; // Will be a big negative number if inactive
						}
						if (cols[util::to_underlying(MarkerType::LOOP_END)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH; // Will be a big negative number if inactive
						}
					}

					value = (markerType < MarkerType::LOOP_END) ? getStartPosFromCol(x) : getEndPosFromCol(x);

					{
						uint32_t lengthInSamples = waveformBasicNavigator.sample->lengthInSamples;

						// Only the END marker, and only in some cases, is allowed to be further right than the waveform
						// length
						if (markerType == MarkerType::END && shouldAllowExtraScrollRight()) {
							if (x > cols[util::to_underlying(markerType)].colOnScreen
							    && value < cols[util::to_underlying(markerType)].pos) {
								return ActionResult::DEALT_WITH; // Probably not actually necessary
							}
							if (value > lengthInSamples && value < lengthInSamples + waveformBasicNavigator.xZoom) {
								value = lengthInSamples;
							}
						}

						else {
							if (value > lengthInSamples) {
								value = lengthInSamples;
							}
						}
					}

					blinkPhase = 0;

doWriteValue:
					writeValue(value);
				}
			}

doRender:
			uiNeedsRendering(this, 0xFFFFFFFF, 0);
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			else {
				displayText();
			}
		}

		// Release press
		else {
			if (currentUIMode == UI_MODE_HOLDING_SAMPLE_MARKER) {
				if (x == pressX && y == pressY) {
					currentUIMode = UI_MODE_NONE;
				}
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

ActionResult SampleMarkerEditor::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Back button
	if (b == BACK) {
		if (on && !currentUIMode) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			exitUI();
		}
	}

	// Horizontal encoder button
	else if (b == X_ENC) {
		if (on) {
			if (isNoUIModeActive() || isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
				currentUIMode |= UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON;
			}
		}

		else {
			exitUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
		}
	}

	else {
		return ActionResult::NOT_DEALT_WITH;
	}

	return ActionResult::DEALT_WITH;
}

bool SampleMarkerEditor::exitUI() {
	display->setNextTransitionDirection(-1);
	close();
	return true;
}

static const uint32_t zoomUIModes[] = {UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON, UI_MODE_AUDITIONING, 0};

ActionResult SampleMarkerEditor::horizontalEncoderAction(int32_t offset) {
	if (isLoopLocked() && Buttons::isShiftButtonPressed()) {
		auto& sampleHolder = getCurrentMultisampleRange().sampleHolder;

		uint32_t proposedEnd = sampleHolder.loopEndPos;

		if (offset > 0) { // turn clockwise
			uint32_t end = sampleHolder.endPos;
			uint32_t loopEnd = sampleHolder.loopEndPos;
			uint32_t loopLength = sampleHolder.loopLength();
			proposedEnd = sampleHolder.loopStartPos + 2 * loopLength;

			if (proposedEnd <= end) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_LOOP_DOUBLED));
			}
			else {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_LOOP_TOO_LONG));
				return ActionResult::DEALT_WITH;
			}
		}
		else { // turn anti-clockwise
			uint32_t loopLength = sampleHolder.loopLength();

			if (loopLength > 2) {
				proposedEnd = sampleHolder.loopStartPos + loopLength / 2;
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_LOOP_HALVED));
			}
			else {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_LOOP_TOO_SHORT));
				return ActionResult::DEALT_WITH;
			}
		}

		// temporarily unlock the loop so we can write the end without moving the start
		sampleHolder.loopLocked = false;
		writeValue(proposedEnd, MarkerType::LOOP_END);
		sampleHolder.loopLocked = true;

		uiNeedsRendering(this, 0xFFFFFFFF, 0);

		return ActionResult::DEALT_WITH;
	}

	// We're quite likely going to need to read the SD card to do either scrolling or zooming
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	MarkerColumn* colsToSend = nullptr;
	MarkerColumn cols[kNumMarkerTypes];
	if (markerType != MarkerType::NONE) {
		getColsOnScreen(cols);
		colsToSend = cols;
	}

	bool success = false;

	// Zoom
	if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
		if (isUIModeWithinRange(zoomUIModes)) {
			success = waveformBasicNavigator.zoom(offset, shouldAllowExtraScrollRight(), colsToSend, markerType);
			if (success) {
				uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
			}
		}
	}

	// Scroll
	else if (isUIModeWithinRange(&zoomUIModes[1])) { // Allow during auditioning only
		success = waveformBasicNavigator.scroll(offset, shouldAllowExtraScrollRight(), colsToSend);

		if (success) {
			uiNeedsRendering(this, 0xFFFFFFFF, 0);
		}
	}

	if (success) {
		recordScrollAndZoom();
		blinkPhase = 0;
	}
	return ActionResult::DEALT_WITH;
}

// Just for the blinking marker I think
ActionResult SampleMarkerEditor::timerCallback() {

	MarkerColumn cols[kNumMarkerTypes];
	getColsOnScreen(cols);

	bool anyMarkerVisible = false;
	MarkerType otherMarker = MarkerType::NONE;
	int32_t otherMarkerX = kInvalidColumn;

	if ((markerType == MarkerType::LOOP_START || markerType == MarkerType::LOOP_END) && isLoopLocked()) {
		// blink both columns when the loop is locked
		otherMarker = (markerType == MarkerType::LOOP_START) ? MarkerType::LOOP_END : MarkerType::LOOP_START;
		otherMarkerX = cols[util::to_underlying(otherMarker)].colOnScreen;
		if (0 <= otherMarkerX && otherMarkerX < kDisplayWidth) {
			anyMarkerVisible = true;
		}
		else {
			otherMarker = MarkerType::NONE;
		}
	}

	int32_t x = cols[util::to_underlying(markerType)].colOnScreen;
	if (0 <= x && x < kDisplayWidth) {
		anyMarkerVisible = true;
	}

	if (!anyMarkerVisible) {
		// No markers visible, no need to re-schedule the timer or do any rendering
		return ActionResult::DEALT_WITH;
	}

	blinkPhase = static_cast<int8_t>((blinkPhase + 1) % 4);

	int32_t supressMask = 0;

	switch (blinkPhase) {
		// Flash a full column of color for the primary selection
	case 1:
		for (auto& col : PadLEDs::image) {
			col[x] = colours::black;
		}

		waveformRenderer.renderOneCol(waveformBasicNavigator.sample, x, PadLEDs::image,
		                              &waveformBasicNavigator.renderData);
		renderMarkerInCol(x, PadLEDs::image, markerType, 0, kDisplayHeight, false);
		PadLEDs::sortLedsForCol(x);
		break;
	case 3:
		// 3 case: supress the primary and locked markers
		supressMask |= 1 << (util::to_underlying(markerType));
		supressMask |= 1 << (util::to_underlying(otherMarker));
		[[fallthrough]];
	case 0:
		// 0 or 2: render normally
		[[fallthrough]];
	case 2:
		if (otherMarker != MarkerType::NONE && otherMarkerX != x) {
			// Need to clear both columns
			for (auto& col : PadLEDs::image) {
				col[x] = colours::black;
				col[otherMarkerX] = colours::black;
			}

			waveformRenderer.renderOneCol(waveformBasicNavigator.sample, x, PadLEDs::image,
			                              &waveformBasicNavigator.renderData);
			waveformRenderer.renderOneCol(waveformBasicNavigator.sample, otherMarkerX, PadLEDs::image,
			                              &waveformBasicNavigator.renderData);

			renderColumn(x, PadLEDs::image, cols, supressMask);
			renderColumn(otherMarkerX, PadLEDs::image, cols, supressMask);

			PadLEDs::sortLedsForCol(x);
			PadLEDs::sortLedsForCol(otherMarkerX);
		}
		else {
			// Only 1 marker to render or both markers are on the same column, only clear and re-render once
			for (auto& col : PadLEDs::image) {
				col[x] = colours::black;
			}

			waveformRenderer.renderOneCol(waveformBasicNavigator.sample, x, PadLEDs::image,
			                              &waveformBasicNavigator.renderData);

			// render the selected marker solid, and flash the rest of the column with the color for the other marker
			renderColumn(x, PadLEDs::image, cols, supressMask);
			PadLEDs::sortLedsForCol(x);
		}

		break;
	default:
		__builtin_unreachable();
	}

	PIC::flush();

	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kSampleMarkerBlinkTime);

	return ActionResult::DEALT_WITH;
}

ActionResult SampleMarkerEditor::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(deluge::hid::button::X_ENC)
	    || getCurrentClip()->type == ClipType::AUDIO) {
		return ActionResult::DEALT_WITH;
	}

	// Must say these buttons were not pressed, or else editing might take place
	ActionResult result = instrumentClipView.verticalEncoderAction(offset, inCardRoutine);

	if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
		return result;
	}

	if (getRootUI() == &keyboardScreen) {
		uiNeedsRendering(this, 0, 0xFFFFFFFF);
	}

	return result;
}

bool SampleMarkerEditor::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                       uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (getRootUI() != &keyboardScreen) {
		return false;
	}
	return instrumentClipView.renderSidebar(whichRows, image, occupancyMask);
}

void SampleMarkerEditor::graphicsRoutine() {

#ifdef TEST_SAMPLE_LOOP_POINTS
	if (!currentlyAccessingCard && !(getNoise() >> 27)) {
		Uart::println("random change to marker -----------------------------");

		int32_t minDistance = 1;

		uint8_t r = getRandom255();

		if (r < 64) {

			// Change start
			Uart::println("change loop start -------------------------------");

			if (!soundEditor.currentSource->reversed) {

				int32_t newStartPos = ((uint32_t)getNoise() % (kSampleRate * 120)) + 10 * kSampleRate;

				if (newStartPos > soundEditor.currentMultiRange->endPos - minDistance)
					newStartPos = soundEditor.currentMultiRange->endPos - minDistance;
				if (soundEditor.currentMultiRange->loopEndPos
				    && newStartPos >= soundEditor.currentMultiRange->loopEndPos - minDistance)
					newStartPos = soundEditor.currentMultiRange->loopEndPos - minDistance;

				writeValue(newStartPos, MarkerType::START);
			}

			else {

				// writeValue(soundEditor.currentMultisampleRange->sample->lengthInSamples, MarkerType::END);

				// int32_t newStartPos = soundEditor.currentMultisampleRange->sample->lengthInSamples;// -
				// (((uint32_t)getNoise() % (kSampleRate * 120)) + 10 * kSampleRate);
				int32_t newStartPos = soundEditor.currentMultiRange->sample->lengthInSamples
				                      - (((uint32_t)getNoise() % (kSampleRate * 12)) + 0 * kSampleRate);

				if (newStartPos < soundEditor.currentMultiRange->startPos + minDistance)
					newStartPos = soundEditor.currentMultiRange->startPos + minDistance;
				if (soundEditor.currentMultiRange->loopStartPos
				    && newStartPos <= soundEditor.currentMultiRange->loopStartPos + minDistance)
					newStartPos = soundEditor.currentMultiRange->loopStartPos + minDistance;

				writeValue(newStartPos, MarkerType::END);
			}
		}

		else { // if (r < 128) {
			// Change loop point

			if (!soundEditor.currentSource->reversed) {

				int32_t newLoopEndPos;
				if (soundEditor.currentMultiRange->loopEndPos) {
					Uart::println("remove loop end -------------------------------");
					newLoopEndPos = 0;
				}
				else {
					Uart::println("set loop end -------------------------------");
					newLoopEndPos = soundEditor.currentMultiRange->startPos + minDistance
					                + ((uint32_t)getNoise() % (kSampleRate * 1));

					if (newLoopEndPos > soundEditor.currentMultiRange->endPos)
						newLoopEndPos = soundEditor.currentMultiRange->endPos;
				}

				writeValue(newLoopEndPos, MarkerType::LOOP_END);
			}
			else {

				int32_t newLoopEndPos;
				if (soundEditor.currentMultiRange->loopStartPos) {
					Uart::println("remove loop end -------------------------------");
					newLoopEndPos = 0;
				}
				else {
					Uart::println("set loop end -------------------------------");
					newLoopEndPos = soundEditor.currentMultiRange->endPos - minDistance
					                - ((uint32_t)getNoise() % (kSampleRate * 1));

					if (newLoopEndPos < soundEditor.currentMultiRange->startPos)
						newLoopEndPos = soundEditor.currentMultiRange->startPos;
				}

				writeValue(newLoopEndPos, MarkerType::LOOP_START);
			}
		}

		Uart::println("end random change");
	}
#endif

	if (PadLEDs::flashCursor == FLASH_CURSOR_OFF) {
		return;
	}

	int32_t newTickSquare = 255;

	VoiceSample* voiceSample = nullptr;
	SamplePlaybackGuide* guide = nullptr;

	// InstrumentClips / Samples
	if (getCurrentClip()->type == ClipType::INSTRUMENT) {
		if (soundEditor.currentSound->hasActiveVoices()) {
			auto valid_voices_view =
			    soundEditor.currentSound->voices() | std::views::filter([](const Sound::ActiveVoice& voice) {
				    // Ensure correct MultisampleRange.
				    return voice->guides[soundEditor.currentSourceIndex].audioFileHolder
				           == soundEditor.currentMultiRange->getAudioFileHolder();
			    });

			if (!valid_voices_view.empty()) {
				auto& assigned_voice = *std::ranges::max_element(valid_voices_view, {}, &Voice::orderSounded);

				VoiceUnisonPartSource* part = &assigned_voice->unisonParts[soundEditor.currentSound->numUnison >> 1]
				                                   .sources[soundEditor.currentSourceIndex];
				if (part != nullptr && part->active) {
					voiceSample = part->voiceSample;
					guide = &assigned_voice->guides[soundEditor.currentSourceIndex];
				}
			}
		}
	}

	// AudioClips
	else {
		voiceSample = getCurrentAudioClip()->voiceSample;
		guide = &getCurrentAudioClip()->guide;
	}

	if (voiceSample) {
		int32_t samplePos = voiceSample->getPlaySample(waveformBasicNavigator.sample, guide);
		if (samplePos >= waveformBasicNavigator.xScroll) {
			newTickSquare = (samplePos - waveformBasicNavigator.xScroll) / waveformBasicNavigator.xZoom;
			if (newTickSquare >= kDisplayWidth) {
				newTickSquare = 255;
			}
		}
	}

	uint8_t tickSquares[kDisplayHeight];
	memset(tickSquares, newTickSquare, kDisplayHeight);
	PadLEDs::setTickSquares(tickSquares, zeroes);
}

bool SampleMarkerEditor::shouldAllowExtraScrollRight() {

	if (markerType == MarkerType::NONE || getCurrentSampleControls()->reversed) {
		return false;
	}

	if (getCurrentClip()->type == ClipType::AUDIO) {
		return true;
	}
	else {
		return (soundEditor.currentSource->repeatMode == SampleRepeatMode::STRETCH
		        || soundEditor.currentSource->repeatMode == SampleRepeatMode::PHASE_LOCKED);
	}
}

void SampleMarkerEditor::renderMarkerInCol(int32_t xDisplay,
                                           RGB thisImage[kDisplayHeight][kDisplayWidth + kSideBarWidth],
                                           MarkerType type, int32_t yStart, int32_t yEnd, bool dimmly) {
	type = reverseRemap(type);

	RGB markerColour{};

	switch (type) {
	case MarkerType::NOT_AVAILABLE:
		[[fallthrough]];
	case MarkerType::NONE:
		markerColour = colours::yellow;
		break;
	case MarkerType::START:
		markerColour = colours::green;
		break;
	case MarkerType::LOOP_START:
		markerColour = colours::cyan;
		break;
	case MarkerType::LOOP_END:
		markerColour = colours::magenta;
		break;
	case MarkerType::END:
		markerColour = colours::red;
		break;
	}

	if (dimmly) {
		markerColour = markerColour.dim(3);
	}

	for (int32_t y = yStart; y < yEnd; ++y) {
		uint8_t value = thisImage[y][xDisplay][0];
		value /= 4;

		thisImage[y][xDisplay] =
		    markerColour.transform([value](uint8_t a) { return value + static_cast<uint8_t>((uint32_t{a} * 3) / 4); });
	}
}

void SampleMarkerEditor::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	using deluge::hid::display::OLED;

	MarkerColumn cols[kNumMarkerTypes];
	getColsOnScreen(cols);

	uint32_t markerPosSamples = cols[util::to_underlying(markerType)].pos;

	char const* markerTypeText;
	switch (reverseRemap(markerType)) {
	case MarkerType::START:
		markerTypeText = "Start point";
		break;

	case MarkerType::END:
		markerTypeText = "End point";
		break;

	case MarkerType::LOOP_START:
		markerTypeText = "Loop start";
		break;

	case MarkerType::LOOP_END:
		markerTypeText = "Loop end";
		break;

	default:
		__builtin_unreachable();
	}

	canvas.drawScreenTitle(markerTypeText);

	if (isLoopLocked()) {
		canvas.drawGraphicMultiLine(OLED::lockIcon, OLED_MAIN_WIDTH_PIXELS - 10, OLED_MAIN_TOPMOST_PIXEL + 1, 7);
		canvas.invertArea(OLED_MAIN_WIDTH_PIXELS - 10, 7, OLED_MAIN_TOPMOST_PIXEL + 9, OLED_MAIN_TOPMOST_PIXEL + 9);
	}

	int32_t smallTextSpacingX = kTextSpacingX;
	int32_t smallTextSizeY = kTextSpacingY;
	int32_t yPixel = OLED_MAIN_TOPMOST_PIXEL + 17;
	int32_t xPixel = 1;

	uint32_t hours = 0;
	uint32_t minutes = 0;
	uint64_t hundredmilliseconds =
	    (uint64_t)markerPosSamples * 100000 / waveformBasicNavigator.sample->sampleRate; // mSec

	if (hundredmilliseconds >= 6000000) {
		minutes = hundredmilliseconds / 6000000;
		hundredmilliseconds -= (uint64_t)minutes * 6000000;

		if (minutes >= 60) {
			hours = minutes / 60;
			minutes -= hours * 60;

			char buffer[12];
			intToString(hours, buffer);
			canvas.drawString(buffer, xPixel, yPixel, smallTextSpacingX, smallTextSizeY);
			xPixel += strlen(buffer) * smallTextSpacingX;

			canvas.drawChar('h', smallTextSpacingX, yPixel, smallTextSpacingX, smallTextSizeY);
			xPixel += smallTextSpacingX * 2;
		}

		char buffer[12];
		intToString(minutes, buffer);
		canvas.drawString(buffer, xPixel, yPixel, smallTextSpacingX, smallTextSizeY);
		xPixel += strlen(buffer) * smallTextSpacingX;

		canvas.drawChar('m', smallTextSpacingX, yPixel, smallTextSpacingX, smallTextSizeY);
		xPixel += smallTextSpacingX * 2;
	}
	else {
		goto printSeconds;
	}

	if (hundredmilliseconds) {
printSeconds:
		int32_t numDecimalPlaces;

		// Maybe we just want to display->millisecond resolution (that's S with 3 decimal places)...
		if (hours || minutes || hundredmilliseconds >= 100000) {
			hundredmilliseconds /= 100;
			numDecimalPlaces = 3;
		}

		// Or, display milliseconds with 2 decimal places - very fine resolution.
		else {
			numDecimalPlaces = 2;
		}

		char buffer[13];
		intToString(hundredmilliseconds, buffer, numDecimalPlaces + 1);
		int32_t length = strlen(buffer);
		memmove(&buffer[length - numDecimalPlaces + 1], &buffer[length - numDecimalPlaces], numDecimalPlaces + 1);
		buffer[length - numDecimalPlaces] = '.';

		canvas.drawString(buffer, xPixel, yPixel, smallTextSpacingX, smallTextSizeY);
		xPixel += (length + 1) * smallTextSpacingX;

		if (hours || minutes) {
			canvas.drawChar('s', xPixel, yPixel, smallTextSpacingX, smallTextSizeY);
		}
		else {
			xPixel += smallTextSpacingX;
			char const* secString = (numDecimalPlaces == 2) ? "msec" : "sec";
			canvas.drawString(secString, xPixel, yPixel, smallTextSpacingX, smallTextSizeY);
		}
	}

	yPixel += 11;

	// Sample count
	xPixel = 1;

	canvas.drawChar('(', xPixel, yPixel, smallTextSpacingX, smallTextSizeY);
	xPixel += smallTextSpacingX;

	char buffer[12];
	intToString(markerPosSamples, buffer);
	canvas.drawString(buffer, xPixel, yPixel, smallTextSpacingX, smallTextSizeY);
	xPixel += smallTextSpacingX * (strlen(buffer) + 1);

	canvas.drawString("smpl)", xPixel, yPixel, smallTextSpacingX, smallTextSizeY);
	xPixel += smallTextSpacingX * 6;
}

void SampleMarkerEditor::loopUnlock() {
	auto& multiSampleRange = getCurrentMultisampleRange();
	multiSampleRange.sampleHolder.loopLocked = false;
	display->displayPopup("FREE");
}

void SampleMarkerEditor::loopLock() {
	auto& multiSampleRange = getCurrentMultisampleRange();
	multiSampleRange.sampleHolder.loopLocked = true;
	display->displayPopup("LOCK");
}

void SampleMarkerEditor::displayText() {

	MarkerColumn cols[kNumMarkerTypes];
	getColsOnScreen(cols);

	// Draw decimal number too
	uint32_t markerPos = cols[util::to_underlying(markerType)].pos;
	int32_t number = (uint64_t)markerPos * 1000 / waveformBasicNavigator.sample->sampleRate; // mSec
	int32_t numDecimals = 3;

	while (number > 9999) {
		number /= 10;
		numDecimals--;
	}

	int32_t drawDot = 3 - numDecimals;
	if (drawDot >= kNumericDisplayLength) {
		drawDot = 0x80u;
	}
	else {
		drawDot = (0x80u) | (1 << (kNumericDisplayLength - drawDot - 1));
	}

	if (isLoopLocked()) {
		drawDot |= 0x01u;
	}

	char buffer[5];
	intToString(number, buffer, numDecimals + 1);

	display->setText(buffer, true, drawDot);
}

void SampleMarkerEditor::renderColumn(int32_t col, RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth],
                                      MarkerColumn cols[kNumMarkerTypes], int32_t supressMask) {
	int32_t numMarkers = 0;

	int32_t activeMarkers = 0;

	for (int32_t markerColIdx = util::to_underlying(MarkerType::START);
	     markerColIdx <= util::to_underlying(MarkerType::END); ++markerColIdx) {
		if (cols[markerColIdx].colOnScreen == col) {
			activeMarkers |= 1 << markerColIdx;
			++numMarkers;
		}
	}

	if (numMarkers == 0) {
		return;
	}

	int32_t marker = util::to_underlying(MarkerType::START);
	uint32_t markerMask = 1 << marker;
	while ((markerMask & activeMarkers) == 0) {
		markerMask <<= 1;
		++marker;
	}

	uint32_t selectedMarkerMask = 1 << util::to_underlying(markerType);
	int32_t prevYEnd = kDisplayHeight;
	int32_t seenMarkers = 0;

	bool isSelectedColumn = (selectedMarkerMask & activeMarkers) != 0;

	int32_t baseIncrement = 0;
	int32_t selectedIncrement;
	switch (numMarkers) {
	case 1:
		baseIncrement = selectedIncrement = kDisplayHeight / 2;
		break;
	case 2:
		baseIncrement = selectedIncrement = 2;
		break;
	case 3:
		baseIncrement = 1;
		selectedIncrement = 2;
		break;
	case 4:
		baseIncrement = selectedIncrement = 1;
		break;
	default:
		__builtin_unreachable();
	}

	while (markerMask != (1 << kNumMarkerTypes)) {
		// skip inactive markers
		if ((markerMask & activeMarkers) == 0) {
			markerMask <<= 1;
			++marker;
			continue;
		}

		bool isSelected = (markerMask & selectedMarkerMask) != 0;

		seenMarkers++;

		int32_t yEnd = prevYEnd;
		int32_t yStart = 0;

		if (seenMarkers == numMarkers) {
			// This is the last marker we need to render in this column, fill the rest of the column
			yStart = kDisplayHeight / 2;
		}
		else {
			// otherwise, render just 1 pad for this marker
			if (isSelected) {
				yStart = yEnd - selectedIncrement;
			}
			else {
				yStart = yEnd - baseIncrement;
			}
		}

		bool allowed = (markerMask & supressMask) == 0u;
		if (allowed) {
			bool dim = !isSelected;
			renderMarkerInCol(col, image, static_cast<MarkerType>(marker), yStart, yEnd, dim);
			renderMarkerInCol(col, image, static_cast<MarkerType>(marker), kDisplayHeight - yEnd,
			                  kDisplayHeight - yStart, dim);
		}

		prevYEnd = yStart;
		markerMask <<= 1;
		++marker;
	}
}

bool SampleMarkerEditor::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                        uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                        bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
	                                  waveformBasicNavigator.xZoom, image, &waveformBasicNavigator.renderData);

	if (markerType != MarkerType::NONE) {
		MarkerColumn cols[kNumMarkerTypes];
		getColsOnScreen(cols);

		int32_t supressMask = 0;
		if (blinkPhase >= 1) {
			supressMask |= 1 << (util::to_underlying(markerType));
			if (isLoopLocked()) {
				if (markerType == MarkerType::LOOP_START) {
					supressMask |= 1 << (util::to_underlying(MarkerType::LOOP_END));
				}
				if (markerType == MarkerType::LOOP_END) {
					supressMask |= 1 << (util::to_underlying(MarkerType::LOOP_START));
				}
			}
		}

		int32_t selectedMarkerCol = cols[util::to_underlying(markerType)].colOnScreen;
		for (int32_t col = 0; col < kDisplayWidth; ++col) {
			if (col == selectedMarkerCol && blinkPhase == 1) {
				renderMarkerInCol(col, image, markerType, 0, kDisplayHeight, false);
			}
			else {

				renderColumn(col, image, cols, supressMask);
			}
		}

		if (cols[util::to_underlying(markerType)].colOnScreen >= 0
		    && cols[util::to_underlying(markerType)].colOnScreen < kDisplayWidth) {
			uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kSampleMarkerBlinkTime);
		}
	}

	return true;
}
