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

#include <AudioEngine.h>
#include <InstrumentClipView.h>
#include <sound.h>
#include <SampleMarkerEditor.h>
#include <WaveformRenderer.h>
#include "KeyboardScreen.h"
#include "matrixdriver.h"
#include "uitimermanager.h"
#include "soundeditor.h"
#include "MultisampleRange.h"
#include "numericdriver.h"
#include "Sample.h"
#include "source.h"
#include "instrument.h"
#include "VoiceSample.h"
#include "WaveformBasicNavigator.h"
#include "song.h"
#include "AudioClip.h"
#include "PadLEDs.h"
#include "Buttons.h"
#include "voice.h"
#include "VoiceVector.h"
#include "extern.h"
#include "ModelStack.h"
#include "playbackhandler.h"
#include "oled.h"

extern "C" {
#include "sio_char.h"
#include "cfunctions.h"
}

const uint8_t zeroes[] = {0, 0, 0, 0, 0, 0, 0, 0};

SampleMarkerEditor sampleMarkerEditor;

SampleMarkerEditor::SampleMarkerEditor() {
}

SampleHolder* getCurrentSampleHolder() {
	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) return &((AudioClip*)currentSong->currentClip)->sampleHolder;
	else return &((MultisampleRange*)soundEditor.currentMultiRange)->sampleHolder;
}

MultisampleRange* getCurrentMultisampleRange() {
	return (MultisampleRange*)soundEditor.currentMultiRange;
}

SampleControls* getCurrentSampleControls() {
	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO)
		return &((AudioClip*)currentSong->currentClip)->sampleControls;
	else return &soundEditor.currentSource->sampleControls;
}

bool SampleMarkerEditor::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	*cols = 0b10;
	return true;
}

bool SampleMarkerEditor::opened() {

	if (getRootUI() == &keyboardScreen) PadLEDs::skipGreyoutFade();

	uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);

	waveformBasicNavigator.sample = (Sample*)getCurrentSampleHolder()->audioFile;

	if (!waveformBasicNavigator.sample) {
		numericDriver.displayPopup(HAVE_OLED ? "No sample" : "CANT");
		return false;
	}

	waveformBasicNavigator.opened(getCurrentSampleHolder());

	blinkInvisible = false;

	uiNeedsRendering(this, 0xFFFFFFFF, 0);

#if !HAVE_OLED
	displayText();
#endif

	if (getRootUI() != &instrumentClipView) {
		renderingNeededRegardlessOfUI(0, 0xFFFFFFFF);
	}

	focusRegained();
	return true;
}

void SampleMarkerEditor::recordScrollAndZoom() {
	if (markerType != MARKER_NONE) {
		getCurrentSampleHolder()->waveformViewScroll = waveformBasicNavigator.xScroll;
		getCurrentSampleHolder()->waveformViewZoom = waveformBasicNavigator.xZoom;
	}
}

void SampleMarkerEditor::writeValue(uint32_t value, int markerTypeNow) {
	if (markerTypeNow == -2) markerTypeNow = markerType;

	int clipType = currentSong->currentClip->type;

	bool audioClipActive;
	if (clipType == CLIP_TYPE_AUDIO) {
		audioClipActive = (playbackHandler.isEitherClockActive() && currentSong->isClipActive(currentSong->currentClip)
		                   && ((AudioClip*)currentSong->currentClip)->voiceSample);

		((AudioClip*)currentSong->currentClip)->unassignVoiceSample();
	}

	if (markerTypeNow == MARKER_START) getCurrentSampleHolder()->startPos = value;
	else if (markerTypeNow == MARKER_LOOP_START) getCurrentMultisampleRange()->sampleHolder.loopStartPos = value;
	else if (markerTypeNow == MARKER_LOOP_END) getCurrentMultisampleRange()->sampleHolder.loopEndPos = value;
	else if (markerTypeNow == MARKER_END) getCurrentSampleHolder()->endPos = value;

	getCurrentSampleHolder()->claimClusterReasons(getCurrentSampleControls()->reversed,
	                                              CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE);

	if (clipType == CLIP_TYPE_AUDIO) {
		if (audioClipActive) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			currentSong->currentClip->resumePlayback(modelStack, true);
		}
	}
	else {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();
		soundEditor.currentSound->sampleZoneChanged(markerTypeNow, soundEditor.currentSourceIndex, modelStack);
		((Instrument*)currentSong->currentClip->output)->beenEdited(true);
	}
}

int SampleMarkerEditor::getStartColOnScreen(int32_t unscrolledPos) {
	return divide_round_negative((int32_t)(unscrolledPos - waveformBasicNavigator.xScroll),
	                             waveformBasicNavigator.xZoom);
}

int SampleMarkerEditor::getEndColOnScreen(int32_t unscrolledPos) {
	return divide_round_negative((int32_t)(unscrolledPos - 1 - waveformBasicNavigator.xScroll),
	                             waveformBasicNavigator.xZoom);
}

int SampleMarkerEditor::getStartPosFromCol(int col) {
	return waveformBasicNavigator.xScroll + col * waveformBasicNavigator.xZoom;
}

int SampleMarkerEditor::getEndPosFromCol(int col) {
	return waveformBasicNavigator.xScroll + (col + 1) * waveformBasicNavigator.xZoom;
}

void SampleMarkerEditor::getColsOnScreen(MarkerColumn* cols) {
	cols[MARKER_START].pos = getCurrentSampleHolder()->startPos;
	cols[MARKER_START].colOnScreen = getStartColOnScreen(cols[MARKER_START].pos);

	if (currentSong->currentClip->type != CLIP_TYPE_AUDIO) {
		cols[MARKER_LOOP_START].pos = getCurrentMultisampleRange()->sampleHolder.loopStartPos;
		cols[MARKER_LOOP_START].colOnScreen =
		    cols[MARKER_LOOP_START].pos ? getStartColOnScreen(cols[MARKER_LOOP_START].pos) : -2147483648;

		cols[MARKER_LOOP_END].pos = getCurrentMultisampleRange()->sampleHolder.loopEndPos;
		cols[MARKER_LOOP_END].colOnScreen =
		    cols[MARKER_LOOP_END].pos ? getEndColOnScreen(cols[MARKER_LOOP_END].pos) : -2147483648;
	}

	else {
		cols[MARKER_LOOP_START].pos = 0;
		cols[MARKER_LOOP_START].colOnScreen = -2147483648;

		cols[MARKER_LOOP_END].pos = 0;
		cols[MARKER_LOOP_END].colOnScreen = -2147483648;
	}

	cols[MARKER_END].pos = getCurrentSampleHolder()->endPos;
	cols[MARKER_END].colOnScreen = getEndColOnScreen(cols[MARKER_END].pos);
}

void SampleMarkerEditor::selectEncoderAction(int8_t offset) {
	if (currentUIMode && currentUIMode != UI_MODE_AUDITIONING) return;

	MarkerColumn cols[NUM_MARKER_TYPES];
	getColsOnScreen(cols);

	int oldCol = cols[markerType].colOnScreen;
	int oldPos = cols[markerType].pos;
	int newCol = oldCol + offset;

	// Make sure we don't drive one marker into the other
	for (int c = 0; c < NUM_MARKER_TYPES; c++) {
		if (c == markerType) continue;
		if (cols[c].colOnScreen == oldCol || cols[c].colOnScreen == newCol) return;
	}

	int32_t newMarkerPos = (markerType < MARKER_LOOP_END) ? getStartPosFromCol(newCol) : getEndPosFromCol(newCol);

	if (newMarkerPos < 0) newMarkerPos = 0;

	if (offset >= 0) {
		if (markerType == MARKER_END && shouldAllowExtraScrollRight()) {
			if (newMarkerPos < oldPos) return;
		}
		else {
			if (newMarkerPos > waveformBasicNavigator.sample->lengthInSamples)
				newMarkerPos = waveformBasicNavigator.sample->lengthInSamples;
		}
	}

	writeValue(newMarkerPos);

	// If marker was on-screen...
	if (oldCol >= 0 && oldCol < displayWidth) {

		getColsOnScreen(cols);
		newCol =
		    cols[markerType]
		        .colOnScreen; // It might have changed, and despite having a newCol variable above, that's only our desired value - we might have run into the end of the sample

		// But isn't anymore...
		if (newCol < 0 || newCol >= displayWidth) {

			// Move scroll
			waveformBasicNavigator.xScroll += waveformBasicNavigator.xZoom * offset;

			if (waveformBasicNavigator.xScroll < 0) waveformBasicNavigator.xScroll = 0; // Shouldn't happen...

			recordScrollAndZoom();
		}
	}

	blinkInvisible = false;

	uiNeedsRendering(this, 0xFFFFFFFF, 0);
#if HAVE_OLED
	renderUIsForOled();
#else
	displayText();
#endif
}

int SampleMarkerEditor::padAction(int x, int y, int on) {

	if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

	if (currentUIMode != UI_MODE_AUDITIONING) { // Don't want to do this while auditioning - too easy to do by mistake
		int soundEditorResult = soundEditor.potentialShortcutPadAction(x, y, on);
		if (soundEditorResult != ACTION_RESULT_NOT_DEALT_WITH) return soundEditorResult;
	}

	// Audition pads - pass to UI beneath
	if (x == displayWidth + 1) {
		if (currentSong->currentClip->type == CLIP_TYPE_INSTRUMENT) instrumentClipView.padAction(x, y, on);
		return ACTION_RESULT_DEALT_WITH;
	}

	// Mute pads
	else if (x == displayWidth) {
		if (on && !currentUIMode) exitUI();
	}

	else {

		// Press down
		if (on) {

			if (currentUIMode && currentUIMode != UI_MODE_AUDITIONING && currentUIMode != UI_MODE_HOLDING_SAMPLE_MARKER)
				return ACTION_RESULT_DEALT_WITH;

			MarkerColumn cols[NUM_MARKER_TYPES];
			getColsOnScreen(cols);

			// See which one we pressed
			int markerPressed = -1;
			for (int m = 0; m < NUM_MARKER_TYPES; m++) {
				if (cols[m].colOnScreen == x) {
					if (markerPressed != -1)
						return ACTION_RESULT_DEALT_WITH; // Get out if there are two markers occupying the same col we pressed
					markerPressed = m;
				}
			}

			int32_t value;

			// If already holding a marker down...
			if (currentUIMode == UI_MODE_HOLDING_SAMPLE_MARKER) {

				if (currentSong->currentClip->type == CLIP_TYPE_INSTRUMENT) {
					// See which one we were holding down
					int markerHeld = -1;
					for (int m = 0; m < NUM_MARKER_TYPES; m++) {
						if (cols[m].colOnScreen == pressX) {
							markerHeld = m;
						}
					}

					// -----------------------------------------------------------
					int newMarkerType;
					int32_t newValue;

					// If start or end, add a loop point
					if (markerHeld == MARKER_START) {

						// Unless we actually just tapped the already existing loop point
						if (x == cols[MARKER_LOOP_START].colOnScreen) {
							markerType = MARKER_LOOP_START;
							value = 0;
							writeValue(value);
							markerType = MARKER_START; // Switch it back
							goto doRender;
						}

						// Limit position
						if (cols[MARKER_START].colOnScreen >= x) return ACTION_RESULT_DEALT_WITH;
						if (getCurrentMultisampleRange()->sampleHolder.loopEndPos
						    && cols[MARKER_LOOP_END].colOnScreen <= x)
							return ACTION_RESULT_DEALT_WITH;
						if (cols[MARKER_END].colOnScreen <= x) return ACTION_RESULT_DEALT_WITH;

						newMarkerType = MARKER_LOOP_START;
						newValue = getStartPosFromCol(x);

ensureNotPastSampleLength:
						// Loop start and end points are not allowed to be further right than the sample waveform length
						if (newValue >= waveformBasicNavigator.sample->lengthInSamples) return ACTION_RESULT_DEALT_WITH;
						markerType = newMarkerType;
						value = newValue;
					}
					else if (markerHeld == MARKER_END) {

						// Unless we actually just tapped the already existing loop point
						if (x == cols[MARKER_LOOP_END].colOnScreen) {
							markerType = MARKER_LOOP_END;
							value = 0;
							writeValue(value);
							markerType = MARKER_END; // Switch it back
							goto doRender;
						}

						// Limit position
						if (cols[MARKER_START].colOnScreen >= x) return ACTION_RESULT_DEALT_WITH;
						if (cols[MARKER_LOOP_START].colOnScreen >= x)
							return ACTION_RESULT_DEALT_WITH; // Will be a big negative number if inactive
						if (cols[MARKER_END].colOnScreen <= x) return ACTION_RESULT_DEALT_WITH;

						newMarkerType = MARKER_LOOP_END;
						newValue = getEndPosFromCol(x);

						goto ensureNotPastSampleLength;
					}

					// Or if a loop point and they pressed the end marker, remove the loop point
					else if (markerHeld == MARKER_LOOP_START) {
						if (x == cols[MARKER_START].colOnScreen) {
							value = 0;
							writeValue(value);
							markerType = MARKER_START;

exitAfterRemovingLoopMarker:
							currentUIMode = UI_MODE_NONE;
							blinkInvisible = true;
							goto doRender;
						}
						else return ACTION_RESULT_DEALT_WITH;
					}
					else if (markerHeld == MARKER_LOOP_END) {
						if (x == cols[MARKER_END].colOnScreen) {
							value = 0;
							writeValue(value);
							markerType = MARKER_END;

							goto exitAfterRemovingLoopMarker;
						}
						else return ACTION_RESULT_DEALT_WITH;
					}

					currentUIMode = UI_MODE_NONE;
					blinkInvisible = false;
					goto doWriteValue;
				}
			}

			// Or if user not already holding a marker down...
			else {

				// If we tapped a marker...
				if (markerPressed >= 0) {
					blinkInvisible = (markerType != markerPressed);
					markerType = markerPressed;
					currentUIMode = UI_MODE_HOLDING_SAMPLE_MARKER;
					pressX = x;
					pressY = y;
				}

				// Otherwise, move the current marker to where we tapped
				else {

					// Make sure it doesn't go past any other markers it shouldn't
					if (markerType == MARKER_START) {
						if (cols[MARKER_LOOP_START].pos && cols[MARKER_LOOP_START].colOnScreen <= x)
							return ACTION_RESULT_DEALT_WITH;
						if (cols[MARKER_LOOP_END].pos && cols[MARKER_LOOP_END].colOnScreen <= x)
							return ACTION_RESULT_DEALT_WITH;
						if (cols[MARKER_END].colOnScreen <= x) return ACTION_RESULT_DEALT_WITH;
					}

					else if (markerType == MARKER_LOOP_START) {
						if (cols[MARKER_START].colOnScreen >= x) return ACTION_RESULT_DEALT_WITH;
						if (cols[MARKER_LOOP_END].pos && cols[MARKER_LOOP_END].colOnScreen <= x)
							return ACTION_RESULT_DEALT_WITH;
						if (cols[MARKER_END].colOnScreen <= x) return ACTION_RESULT_DEALT_WITH;
					}

					else if (markerType == MARKER_LOOP_END) {
						if (cols[MARKER_START].colOnScreen >= x) return ACTION_RESULT_DEALT_WITH;
						if (cols[MARKER_LOOP_START].colOnScreen >= x)
							return ACTION_RESULT_DEALT_WITH; // Will be a big negative number if inactive
						if (cols[MARKER_END].colOnScreen <= x) return ACTION_RESULT_DEALT_WITH;
					}

					else if (markerType == MARKER_END) {
						if (cols[MARKER_START].colOnScreen >= x) return ACTION_RESULT_DEALT_WITH;
						if (cols[MARKER_LOOP_START].colOnScreen >= x)
							return ACTION_RESULT_DEALT_WITH; // Will be a big negative number if inactive
						if (cols[MARKER_LOOP_END].colOnScreen >= x)
							return ACTION_RESULT_DEALT_WITH; // Will be a big negative number if inactive
					}

					value = (markerType < MARKER_LOOP_END) ? getStartPosFromCol(x) : getEndPosFromCol(x);

					{
						uint32_t lengthInSamples = waveformBasicNavigator.sample->lengthInSamples;

						// Only the END marker, and only in some cases, is allowed to be further right than the waveform length
						if (markerType == MARKER_END && shouldAllowExtraScrollRight()) {
							if (x > cols[markerType].colOnScreen && value < cols[markerType].pos)
								return ACTION_RESULT_DEALT_WITH; // Probably not actually necessary
							if (value > lengthInSamples && value < lengthInSamples + waveformBasicNavigator.xZoom)
								value = lengthInSamples;
						}

						else {
							if (value > lengthInSamples) value = lengthInSamples;
						}
					}

					blinkInvisible = false;

doWriteValue:
					writeValue(value);
				}
			}

doRender:
			uiNeedsRendering(this, 0xFFFFFFFF, 0);
#if HAVE_OLED
			renderUIsForOled();
#else
			displayText();
#endif
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

	return ACTION_RESULT_DEALT_WITH;
}

int SampleMarkerEditor::buttonAction(int x, int y, bool on, bool inCardRoutine) {

	// Back button
	if (x == backButtonX && y == backButtonY) {
		if (on && !currentUIMode) {
			if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			exitUI();
		}
	}

	// Horizontal encoder button
	else if (x == xEncButtonX && y == xEncButtonY) {
		if (on) {
			if (isNoUIModeActive() || isUIModeActiveExclusively(UI_MODE_AUDITIONING))
				currentUIMode |= UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON;
		}

		else {
			exitUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
		}
	}

	else return ACTION_RESULT_NOT_DEALT_WITH;

	return ACTION_RESULT_DEALT_WITH;
}

void SampleMarkerEditor::exitUI() {
	numericDriver.setNextTransitionDirection(-1);
	close();
}

static const uint32_t zoomUIModes[] = {UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON, UI_MODE_AUDITIONING, 0};

int SampleMarkerEditor::horizontalEncoderAction(int offset) {

	// We're quite likely going to need to read the SD card to do either scrolling or zooming
	if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

	MarkerColumn* colsToSend = NULL;
	MarkerColumn cols[NUM_MARKER_TYPES];
	if (markerType != MARKER_NONE) {
		getColsOnScreen(cols);
		colsToSend = cols;
	}

	bool success = false;

	// Zoom
	if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
		if (isUIModeWithinRange(zoomUIModes)) {
			success = waveformBasicNavigator.zoom(offset, shouldAllowExtraScrollRight(), colsToSend, markerType);
			if (success) uiTimerManager.unsetTimer(TIMER_UI_SPECIFIC);
		}
	}

	// Scroll
	else if (isUIModeWithinRange(&zoomUIModes[1])) { // Allow during auditioning only
		success = waveformBasicNavigator.scroll(offset, shouldAllowExtraScrollRight(), colsToSend);

		if (success) uiNeedsRendering(this, 0xFFFFFFFF, 0);
	}

	if (success) {
		recordScrollAndZoom();
		blinkInvisible = false;
	}
	return ACTION_RESULT_DEALT_WITH;
}

// Just for the blinking marker I think
int SampleMarkerEditor::timerCallback() {

	MarkerColumn cols[NUM_MARKER_TYPES];
	getColsOnScreen(cols);

	int x = cols[markerType].colOnScreen;
	if (x < 0 || x >= displayWidth)
		return ACTION_RESULT_DEALT_WITH; // Shouldn't happen, but let's be safe - and not set the timer again if it's offscreen

	blinkInvisible = !blinkInvisible;

	// Clear col
	for (int y = 0; y < displayHeight; y++) {
		memset(PadLEDs::image[y][x], 0, 3);
	}

	renderForOneCol(x, PadLEDs::image, cols);

	PadLEDs::sortLedsForCol(x);
	uartFlushIfNotSending(UART_ITEM_PIC_PADS);

	uiTimerManager.setTimer(TIMER_UI_SPECIFIC, SAMPLE_MARKER_BLINK_TIME);

	return ACTION_RESULT_DEALT_WITH;
}

int SampleMarkerEditor::verticalEncoderAction(int offset, bool inCardRoutine) {
	if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(xEncButtonX, xEncButtonY)
	    || currentSong->currentClip->type == CLIP_TYPE_AUDIO)
		return ACTION_RESULT_DEALT_WITH;

	int result = instrumentClipView.verticalEncoderAction(
	    offset, inCardRoutine); // Must say these buttons were not pressed, or else editing might take place

	if (result == ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE) return result;

	if (getRootUI() == &keyboardScreen) {
		uiNeedsRendering(this, 0, 0xFFFFFFFF);
	}

	return result;
}

bool SampleMarkerEditor::renderSidebar(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
                                       uint8_t occupancyMask[][displayWidth + sideBarWidth]) {
	if (getRootUI() != &keyboardScreen) return false;
	return instrumentClipView.renderSidebar(whichRows, image, occupancyMask);
}

void SampleMarkerEditor::graphicsRoutine() {

#ifdef TEST_SAMPLE_LOOP_POINTS
	if (!currentlyAccessingCard && !(getNoise() >> 27)) {
		Uart::println("random change to marker -----------------------------");

		int minDistance = 1;

		uint8_t r = getRandom255();

		if (r < 64) {

			// Change start
			Uart::println("change loop start -------------------------------");

			if (!soundEditor.currentSource->reversed) {

				int newStartPos = ((uint32_t)getNoise() % (44100 * 120)) + 10 * 44100;

				if (newStartPos > soundEditor.currentMultiRange->endPos - minDistance)
					newStartPos = soundEditor.currentMultiRange->endPos - minDistance;
				if (soundEditor.currentMultiRange->loopEndPos
				    && newStartPos >= soundEditor.currentMultiRange->loopEndPos - minDistance)
					newStartPos = soundEditor.currentMultiRange->loopEndPos - minDistance;

				writeValue(newStartPos, MARKER_START);
			}

			else {

				//writeValue(soundEditor.currentMultisampleRange->sample->lengthInSamples, MARKER_END);

				//int newStartPos = soundEditor.currentMultisampleRange->sample->lengthInSamples;// - (((uint32_t)getNoise() % (44100 * 120)) + 10 * 44100);
				int newStartPos = soundEditor.currentMultiRange->sample->lengthInSamples
				                  - (((uint32_t)getNoise() % (44100 * 12)) + 0 * 44100);

				if (newStartPos < soundEditor.currentMultiRange->startPos + minDistance)
					newStartPos = soundEditor.currentMultiRange->startPos + minDistance;
				if (soundEditor.currentMultiRange->loopStartPos
				    && newStartPos <= soundEditor.currentMultiRange->loopStartPos + minDistance)
					newStartPos = soundEditor.currentMultiRange->loopStartPos + minDistance;

				writeValue(newStartPos, MARKER_END);
			}
		}

		else { //if (r < 128) {
			// Change loop point

			if (!soundEditor.currentSource->reversed) {

				int newLoopEndPos;
				if (soundEditor.currentMultiRange->loopEndPos) {
					Uart::println("remove loop end -------------------------------");
					newLoopEndPos = 0;
				}
				else {
					Uart::println("set loop end -------------------------------");
					newLoopEndPos =
					    soundEditor.currentMultiRange->startPos + minDistance + ((uint32_t)getNoise() % (44100 * 1));

					if (newLoopEndPos > soundEditor.currentMultiRange->endPos)
						newLoopEndPos = soundEditor.currentMultiRange->endPos;
				}

				writeValue(newLoopEndPos, MARKER_LOOP_END);
			}
			else {

				int newLoopEndPos;
				if (soundEditor.currentMultiRange->loopStartPos) {
					Uart::println("remove loop end -------------------------------");
					newLoopEndPos = 0;
				}
				else {
					Uart::println("set loop end -------------------------------");
					newLoopEndPos =
					    soundEditor.currentMultiRange->endPos - minDistance - ((uint32_t)getNoise() % (44100 * 1));

					if (newLoopEndPos < soundEditor.currentMultiRange->startPos)
						newLoopEndPos = soundEditor.currentMultiRange->startPos;
				}

				writeValue(newLoopEndPos, MARKER_LOOP_START);
			}
		}

		Uart::println("end random change");
	}
#endif

	if (PadLEDs::flashCursor == FLASH_CURSOR_OFF) return;

	int newTickSquare = 255;

	VoiceSample* voiceSample = NULL;
	SamplePlaybackGuide* guide = NULL;

	// InstrumentClips / Samples
	if (currentSong->currentClip->type == CLIP_TYPE_INSTRUMENT) {

		if (soundEditor.currentSound->hasAnyVoices()) {

			Voice* assignedVoice = NULL;

			int ends[2];
			AudioEngine::activeVoices.getRangeForSound(soundEditor.currentSound, ends);
			for (int v = ends[0]; v < ends[1]; v++) {
				Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);

				// Ensure correct MultisampleRange.
				if (thisVoice->guides[soundEditor.currentSourceIndex].audioFileHolder
				    != soundEditor.currentMultiRange->getAudioFileHolder())
					continue;

				if (!assignedVoice || thisVoice->orderSounded > assignedVoice->orderSounded) {
					assignedVoice = thisVoice;
				}
			}

			if (assignedVoice) {
				VoiceUnisonPartSource* part = &assignedVoice->unisonParts[soundEditor.currentSound->numUnison >> 1]
				                                   .sources[soundEditor.currentSourceIndex];
				if (part && part->active) {
					voiceSample = part->voiceSample;
					guide = &assignedVoice->guides[soundEditor.currentSourceIndex];
				}
			}
		}
	}

	// AudioClips
	else {
		voiceSample = ((AudioClip*)currentSong->currentClip)->voiceSample;
		guide = &((AudioClip*)currentSong->currentClip)->guide;
	}

	if (voiceSample) {
		int samplePos = voiceSample->getPlaySample(waveformBasicNavigator.sample, guide);
		if (samplePos >= waveformBasicNavigator.xScroll) {
			newTickSquare = (samplePos - waveformBasicNavigator.xScroll) / waveformBasicNavigator.xZoom;
			if (newTickSquare >= displayWidth) newTickSquare = 255;
		}
	}

	uint8_t tickSquares[displayHeight];
	memset(tickSquares, newTickSquare, displayHeight);
	PadLEDs::setTickSquares(tickSquares, zeroes);
}

bool SampleMarkerEditor::shouldAllowExtraScrollRight() {

	if (markerType == MARKER_NONE || getCurrentSampleControls()->reversed) return false;

	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {
		return true;
	}
	else {
		return (soundEditor.currentSource->repeatMode == SAMPLE_REPEAT_STRETCH);
	}
}

void SampleMarkerEditor::renderForOneCol(int xDisplay, uint8_t thisImage[displayHeight][displayWidth + sideBarWidth][3],
                                         MarkerColumn* cols) {

	waveformRenderer.renderOneCol(waveformBasicNavigator.sample, xDisplay, thisImage,
	                              &waveformBasicNavigator.renderData);

	renderMarkersForOneCol(xDisplay, thisImage, cols);
}

void SampleMarkerEditor::renderMarkersForOneCol(int xDisplay,
                                                uint8_t thisImage[displayHeight][displayWidth + sideBarWidth][3],
                                                MarkerColumn* cols) {

	if (markerType != MARKER_NONE) {

		bool reversed = getCurrentSampleControls()->reversed;

		int greenMarker = reversed ? MARKER_END : MARKER_START;
		int cyanMarker = reversed ? MARKER_LOOP_END : MARKER_LOOP_START;
		int purpleMarker = reversed ? MARKER_LOOP_START : MARKER_LOOP_END;
		int redMarker = reversed ? MARKER_START : MARKER_END;

		unsigned int markersActiveHere = 0;
		for (int m = 0; m < NUM_MARKER_TYPES; m++) {
			markersActiveHere |= (xDisplay == cols[m].colOnScreen && !(blinkInvisible && markerType == m)) << m;
		}

		if (markersActiveHere) {
			int currentMarkerType = 0;

			for (int y = 0; y < displayHeight; y++) {
				while (!(markersActiveHere & (1 << currentMarkerType))) {
					currentMarkerType++;
					if (currentMarkerType == NUM_MARKER_TYPES) currentMarkerType = 0;
				}

				int existingColourAmount = thisImage[y][xDisplay][0];

				// Green
				if (currentMarkerType == greenMarker) {
					thisImage[y][xDisplay][0] >>= 2;
					thisImage[y][xDisplay][1] = 255 - existingColourAmount * 2;
					thisImage[y][xDisplay][2] >>= 2;
				}

				// Cyan
				else if (currentMarkerType == cyanMarker) {
					thisImage[y][xDisplay][0] >>= 1;
					thisImage[y][xDisplay][1] = 140 - existingColourAmount;
					thisImage[y][xDisplay][2] = 140 - existingColourAmount;
				}

				// Purple
				else if (currentMarkerType == purpleMarker) {
					thisImage[y][xDisplay][0] = 140 - existingColourAmount;
					thisImage[y][xDisplay][1] >>= 1;
					thisImage[y][xDisplay][2] = 140 - existingColourAmount;
				}

				// Red
				else if (currentMarkerType == redMarker) {
					thisImage[y][xDisplay][0] = 255 - existingColourAmount * 2;
					thisImage[y][xDisplay][1] >>= 2;
					thisImage[y][xDisplay][2] >>= 2;
				}

				currentMarkerType++;
				if (currentMarkerType == NUM_MARKER_TYPES) currentMarkerType = 0;
			}
		}
	}
}

#if HAVE_OLED
void SampleMarkerEditor::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	MarkerColumn cols[NUM_MARKER_TYPES];
	getColsOnScreen(cols);

	uint32_t markerPosSamples = cols[markerType].pos;

	char const* markerTypeText;
	switch (markerType) {
	case MARKER_START:
		markerTypeText = "Start point";
		break;

	case MARKER_END:
		markerTypeText = "End point";
		break;

	case MARKER_LOOP_START:
		markerTypeText = "Loop start";
		break;

	case MARKER_LOOP_END:
		markerTypeText = "Loop end";
		break;

	default:
		__builtin_unreachable();
	}

	OLED::drawScreenTitle(markerTypeText);

	int smallTextSpacingX = TEXT_SPACING_X;
	int smallTextSizeY = TEXT_SPACING_Y;
	int yPixel = OLED_MAIN_TOPMOST_PIXEL + 17;
	int xPixel = 1;

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
			OLED::drawString(buffer, xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX,
			                 smallTextSizeY);
			xPixel += strlen(buffer) * smallTextSpacingX;

			OLED::drawChar('h', smallTextSpacingX, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX,
			               smallTextSizeY);
			xPixel += smallTextSpacingX * 2;
		}

		char buffer[12];
		intToString(minutes, buffer);
		OLED::drawString(buffer, xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX, smallTextSizeY);
		xPixel += strlen(buffer) * smallTextSpacingX;

		OLED::drawChar('m', smallTextSpacingX, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX,
		               smallTextSizeY);
		xPixel += smallTextSpacingX * 2;
	}
	else goto printSeconds;

	if (hundredmilliseconds) {
printSeconds:
		int numDecimalPlaces;

		// Maybe we just want to display millisecond resolution (that's S with 3 decimal places)...
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
		int length = strlen(buffer);
		memmove(&buffer[length - numDecimalPlaces + 1], &buffer[length - numDecimalPlaces], numDecimalPlaces + 1);
		buffer[length - numDecimalPlaces] = '.';

		OLED::drawString(buffer, xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX, smallTextSizeY);
		xPixel += (length + 1) * smallTextSpacingX;

		if (hours || minutes) {
			OLED::drawChar('s', xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX, smallTextSizeY);
		}
		else {
			xPixel += smallTextSpacingX;
			char const* secString = (numDecimalPlaces == 2) ? "msec" : "sec";
			OLED::drawString(secString, xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX,
			                 smallTextSizeY);
		}
	}

	yPixel += 11;

	// Sample count
	xPixel = 1;

	OLED::drawChar('(', xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX, smallTextSizeY);
	xPixel += smallTextSpacingX;

	char buffer[12];
	intToString(markerPosSamples, buffer);
	OLED::drawString(buffer, xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX, smallTextSizeY);
	xPixel += smallTextSpacingX * (strlen(buffer) + 1);

	OLED::drawString("smpl)", xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX, smallTextSizeY);
	xPixel += smallTextSpacingX * 6;
}

#else

void SampleMarkerEditor::displayText() {

	MarkerColumn cols[NUM_MARKER_TYPES];
	getColsOnScreen(cols);

	// Draw decimal number too
	uint32_t markerPos = cols[markerType].pos;
	int32_t number = (uint64_t)markerPos * 1000 / waveformBasicNavigator.sample->sampleRate; // mSec
	int numDecimals = 3;

	while (number > 9999) {
		number /= 10;
		numDecimals--;
	}

	int drawDot = 3 - numDecimals;
	if (drawDot >= NUMERIC_DISPLAY_LENGTH) drawDot = 255;

	char buffer[5];
	intToString(number, buffer, numDecimals + 1);

	numericDriver.setText(buffer, true, drawDot);
}
#endif

bool SampleMarkerEditor::renderMainPads(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
                                        uint8_t occupancyMask[][displayWidth + sideBarWidth], bool drawUndefinedArea) {
	if (!image) return true;

	waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
	                                  waveformBasicNavigator.xZoom, image, &waveformBasicNavigator.renderData);

	if (markerType != MARKER_NONE) {
		MarkerColumn cols[NUM_MARKER_TYPES];
		getColsOnScreen(cols);

		for (int xDisplay = 0; xDisplay < displayWidth; xDisplay++) {
			renderMarkersForOneCol(xDisplay, image, cols);
		}

		if (cols[markerType].colOnScreen >= 0 && cols[markerType].colOnScreen < displayWidth) {
			uiTimerManager.setTimer(TIMER_UI_SPECIFIC, SAMPLE_MARKER_BLINK_TIME);
		}
	}

	return true;
}
