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

#include "processing/engines/audio_engine.h"
#include "gui/context_menu/sample_browser/kit.h"
#include "model/clip/instrument_clip.h"
#include "gui/views/instrument_clip_view.h"
#include "modulation/params/param_manager.h"
#include "gui/ui/browser/sample_browser.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "gui/ui/slicer.h"
#include "util/functions.h"
#include "hid/display/numeric_driver.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/kit.h"
#include <new>
#include <string.h>
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "storage/multi_range/multisample_range.h"
#include "model/model_stack.h"
#include "modulation/params/param_set.h"
#include "hid/display/oled.h"
#include "model/voice/voice_vector.h"
#include "model/voice/voice_sample.h"
#include "model/voice/voice.h"
#include "hid/led/pad_leds.h"
#include "gui/waveform/waveform_basic_navigator.h"
#include "hid/matrix/matrix_driver.h"
#include "hid/buttons.h"
#include "gui/waveform/waveform_renderer.h"

extern "C" {
#include "util/cfunctions.h"
}

Slicer slicer{};

Slicer::Slicer() {
#if HAVE_OLED
	oledShowsUIUnderneath = true;
#endif
}

void Slicer::focusRegained() {

	actionLogger.deleteAllLogs();

	numClips = 16;

	numManualSlice = 1;
	currentSlice = 0;
	slicerMode = SLICER_MODE_REGION;
	for (int i = 0; i < MAX_MANUAL_SLICES; i++) {
		manualSlicePoints[i].startPos = 0;
		manualSlicePoints[i].transpose = 0;
	}

#if !HAVE_OLED
	redraw();
#endif
}

#if HAVE_OLED
void Slicer::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {

	int windowWidth = 100;
	int windowHeight = 31;
	int horizontalShift = 6;

	int windowMinX = (OLED_MAIN_WIDTH_PIXELS - windowWidth) >> 1;
	windowMinX += horizontalShift;
	int windowMaxX = windowMinX + windowWidth;

	int windowMinY = (OLED_MAIN_HEIGHT_PIXELS - windowHeight) >> 1;
	windowMinY += 2;
	int windowMaxY = windowMinY + windowHeight;

	OLED::clearAreaExact(windowMinX + 1, windowMinY + 1, windowMaxX - 1, windowMaxY - 1, image);

	OLED::drawRectangle(windowMinX, windowMinY, windowMaxX, windowMaxY, image);
	OLED::drawHorizontalLine(windowMinY + 15, 26, OLED_MAIN_WIDTH_PIXELS - 22, &image[0]);
	OLED::drawString("Num. slices", 30, windowMinY + 6, image[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X,
	                 TEXT_SPACING_Y);
	char buffer[12];
	intToString(slicerMode == SLICER_MODE_REGION ? numClips : numManualSlice, buffer);
	OLED::drawStringCentred(buffer, windowMinY + 18, image[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X, TEXT_SPACING_Y,
	                        (OLED_MAIN_WIDTH_PIXELS >> 1) + horizontalShift);
}

#else

void Slicer::redraw() {
	numericDriver.setTextAsNumber(slicerMode == SLICER_MODE_REGION ? numClips : numManualSlice, 255, true);
}
#endif

bool Slicer::renderMainPads(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
                            uint8_t occupancyMask[][displayWidth + sideBarWidth], bool drawUndefinedArea) {

	if (slicerMode == SLICER_MODE_REGION) {
		uint8_t myImage[displayHeight][displayWidth + sideBarWidth][3];
		waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
		                                  waveformBasicNavigator.xZoom, image, &waveformBasicNavigator.renderData);
	}
	else if (slicerMode == SLICER_MODE_MANUAL) {

		uint8_t myImage[displayHeight][displayWidth + sideBarWidth][3];
		waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
		                                  waveformBasicNavigator.xZoom, myImage, &waveformBasicNavigator.renderData);

		for (int xx = 0; xx < displayWidth; xx++) {
			for (int yy = 0; yy < displayHeight / 2; yy++) {
				image[yy + 4][xx][0] = (myImage[yy * 2][xx][0] + myImage[yy * 2 + 1][xx][0]) / 2;
				image[yy + 4][xx][1] = (myImage[yy * 2][xx][1] + myImage[yy * 2 + 1][xx][1]) / 2;
				image[yy + 4][xx][2] = (myImage[yy * 2][xx][2] + myImage[yy * 2 + 1][xx][2]) / 2;
			}
		}
		for (int i = 0; i < numManualSlice; i++) { // Slices
			int x = manualSlicePoints[i].startPos / (waveformBasicNavigator.sample->lengthInSamples + 0.0) * 16;
			image[4][x][0] = 1;
			image[4][x][1] = (i == currentSlice) ? 200 : 16;
			image[4][x][2] = 1;
		}

		for (int i = 0; i < MAX_MANUAL_SLICES; i++) { // Lower screen
			int xx = (i % 4) + (i / 16) * 4;
			int yy = (i / 4) % 4;
			int page = i / 16;
			uint8_t colour[] = {3, 3, 3};

			if (page % 2 == 0) {
				colour[0] = (i < numManualSlice) ? 0 : 0;
				colour[1] = (i < numManualSlice) ? 0 : 0;
				colour[2] = (i < numManualSlice) ? 64 : 3;
			}
			else {
				colour[0] = (i < numManualSlice) ? 0 : 0;
				colour[1] = (i < numManualSlice) ? 32 : 1;
				colour[2] = (i < numManualSlice) ? 64 : 3;
			}
			if (i == this->currentSlice) {
				colour[0] = 0;
				colour[1] = 127;
				colour[2] = 0;
			}
			memcpy(image[yy][xx], colour, 3);
		}
	}
	return true;
}

const uint8_t zeroes[] = {0, 0, 0, 0, 0, 0, 0, 0};

void Slicer::graphicsRoutine() {

	int newTickSquare = 255;
	VoiceSample* voiceSample = NULL;
	SamplePlaybackGuide* guide = NULL;

	MultisampleRange* range;
	Kit* kit = (Kit*)currentSong->currentClip->output;
	SoundDrum* drum = (SoundDrum*)kit->firstDrum;

	if (currentSong->currentClip->type == CLIP_TYPE_INSTRUMENT) {

		if (drum->hasAnyVoices()) {

			Voice* assignedVoice = NULL;

			range = (MultisampleRange*)drum->sources[0].getOrCreateFirstRange();

			int ends[2];
			AudioEngine::activeVoices.getRangeForSound(drum, ends);
			for (int v = ends[0]; v < ends[1]; v++) {
				Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);

				// Ensure correct MultisampleRange.
				if (thisVoice->guides[0].audioFileHolder != range->getAudioFileHolder()) {
					continue;
				}

				if (!assignedVoice || thisVoice->orderSounded > assignedVoice->orderSounded) {
					assignedVoice = thisVoice;
				}
			}

			if (assignedVoice) {
				VoiceUnisonPartSource* part = &assignedVoice->unisonParts[drum->numUnison >> 1].sources[0];
				if (part && part->active) {
					voiceSample = part->voiceSample;
					guide = &assignedVoice->guides[soundEditor.currentSourceIndex];
				}
			}
		}
	}

	if (voiceSample) {
		int samplePos = voiceSample->getPlaySample((Sample*)range->sampleHolder.audioFile, guide);
		if (samplePos >= waveformBasicNavigator.xScroll) {
			newTickSquare = (samplePos - waveformBasicNavigator.xScroll) / waveformBasicNavigator.xZoom;
			if (newTickSquare >= displayWidth) {
				newTickSquare = 255;
			}
		}
	}

	uint8_t tickSquares[displayHeight];
	memset(tickSquares, 255, displayHeight);
	tickSquares[displayHeight - 1] = newTickSquare;
	tickSquares[displayHeight - 2] = newTickSquare;
	tickSquares[displayHeight - 3] = newTickSquare;
	tickSquares[displayHeight - 4] = newTickSquare;

	PadLEDs::setTickSquares(tickSquares, zeroes);
}

int Slicer::horizontalEncoderAction(int offset) {

	if (slicerMode == SLICER_MODE_MANUAL) {
		int newPos = manualSlicePoints[currentSlice].startPos;
		newPos += ((Buttons::isShiftButtonPressed() == true) ? 10 : 100) * offset;

		if (currentSlice > 0 && currentSlice < numManualSlice - 1) {
			if (newPos <= manualSlicePoints[currentSlice - 1].startPos + 1)
				newPos = manualSlicePoints[currentSlice - 1].startPos + 1;
			if (newPos >= manualSlicePoints[currentSlice + 1].startPos - 1)
				newPos = manualSlicePoints[currentSlice + 1].startPos - 1;
		}

		if (newPos < 0)
			newPos = 0;
		if (newPos > waveformBasicNavigator.sample->lengthInSamples)
			newPos = waveformBasicNavigator.sample->lengthInSamples;
		manualSlicePoints[currentSlice].startPos = newPos;

#if HAVE_OLED
		char buffer[24];
		strcpy(buffer, "Start: ");
		intToString(manualSlicePoints[currentSlice].startPos, buffer + strlen(buffer));
		OLED::popupText(buffer);
#else
		char buffer[12];
		strcpy(buffer, "");
		intToString(manualSlicePoints[currentSlice].startPos / 1000, buffer + strlen(buffer));
		numericDriver.displayPopup(buffer, 0, true);
#endif
		uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
	}
	return ACTION_RESULT_DEALT_WITH;
}

int Slicer::verticalEncoderAction(int offset, bool inCardRoutine) {
	if (slicerMode == SLICER_MODE_MANUAL) {

		manualSlicePoints[currentSlice].transpose += offset;
		if (manualSlicePoints[currentSlice].transpose > 24)
			manualSlicePoints[currentSlice].transpose = 24;
		if (manualSlicePoints[currentSlice].transpose < -24)
			manualSlicePoints[currentSlice].transpose = -24;
#if HAVE_OLED
		char buffer[24];
		strcpy(buffer, "Transpose: ");
		intToString(manualSlicePoints[currentSlice].transpose, buffer + strlen(buffer));
		OLED::popupText(buffer);
#else
		char buffer[12];
		strcpy(buffer, "");
		intToString(manualSlicePoints[currentSlice].transpose, buffer + strlen(buffer));
		numericDriver.displayPopup(buffer, 0, true);
#endif
	}
	return ACTION_RESULT_DEALT_WITH;
}

void Slicer::selectEncoderAction(int8_t offset) {
	if (slicerMode == SLICER_MODE_REGION) {
		numClips += offset;
		if (numClips == 257) {
			numClips = 2;
		}
		else if (numClips == 1) {
			numClips = 256;
		}
	}
	else { //SLICER_MODE_MANUAL
		if (offset < 0) {
			numManualSlice += offset;
			if (numManualSlice <= 0) {
				numManualSlice = 1;
				manualSlicePoints[0].startPos = 0;
				manualSlicePoints[0].transpose = 0;
			}
		}
		if (currentSlice >= numManualSlice - 1) {
			currentSlice = numManualSlice - 1;
		}
		uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
	}
#if HAVE_OLED
	renderUIsForOled();
#else
	redraw();
#endif
}
int Slicer::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	if (currentUIMode != UI_MODE_NONE || !on) {
		return ACTION_RESULT_NOT_DEALT_WITH;
	}

	//switch slicer mode
	if (b == X_ENC && on) {
		slicerMode++;
		slicerMode %= 2;
		if (slicerMode == SLICER_MODE_MANUAL)
			AudioEngine::stopAnyPreviewing();
#if HAVE_OLED
		renderUIsForOled();
#else
		redraw();
#endif

		((Kit*)currentSong->currentClip->output)->firstDrum->unassignAllVoices(); //stop
		uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
		return ACTION_RESULT_DEALT_WITH;
	}

	//pop up Transpose value
	if (b == Y_ENC && on && slicerMode == SLICER_MODE_MANUAL && currentSlice < numManualSlice) {
#if HAVE_OLED
		char buffer[24];
		strcpy(buffer, "Transpose: ");
		intToString(manualSlicePoints[currentSlice].transpose, buffer + strlen(buffer));
		OLED::popupText(buffer);
#else
		char buffer[12];
		strcpy(buffer, "");
		intToString(manualSlicePoints[currentSlice].transpose, buffer + strlen(buffer));
		numericDriver.displayPopup(buffer, 0, true);
#endif
		return ACTION_RESULT_DEALT_WITH;
	}

	//delete slice
	if (b == SAVE && on && slicerMode == SLICER_MODE_MANUAL) {
		int xx = (currentSlice % 4) + (currentSlice / 16) * 4;
		int yy = (currentSlice / 4) % 4;
		if (matrixDriver.isPadPressed(xx, yy) && currentSlice < numManualSlice) {
			int target = currentSlice;

			for (int i = 0; i < MAX_MANUAL_SLICES - 1; i++) {
				manualSlicePoints[i] = manualSlicePoints[(i >= target) ? i + 1 : i];
			}
			manualSlicePoints[MAX_MANUAL_SLICES - 1].startPos = 0;
			manualSlicePoints[MAX_MANUAL_SLICES - 1].transpose = 0;

			numManualSlice--;
			if (numManualSlice <= 0) {
				numManualSlice = 1;
				manualSlicePoints[0].startPos = 0;
				manualSlicePoints[0].transpose = 0;
			}
			if (currentSlice >= numManualSlice - 1) {
				currentSlice = numManualSlice - 1;
			}

			uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
#if HAVE_OLED
			renderUIsForOled();
#else
			redraw();
#endif
			return ACTION_RESULT_DEALT_WITH;
		}
	}

	if (b == SELECT_ENC) {
		if (inCardRoutine) {
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		if (slicerMode == SLICER_MODE_REGION) {
			doSlice();
		}
		else {
			((Kit*)currentSong->currentClip->output)->firstDrum->unassignAllVoices(); //stop
			numClips = numManualSlice;
			doSlice();
			Kit* kit = (Kit*)currentSong->currentClip->output;
			for (int i = 0; i < numManualSlice; i++) {
				Drum* drum = kit->getDrumFromIndex(i);
				SoundDrum* soundDrum = (SoundDrum*)drum;
				MultisampleRange* range = (MultisampleRange*)soundDrum->sources[0].getOrCreateFirstRange();
				Sample* sample = (Sample*)range->sampleHolder.audioFile;
				range->sampleHolder.startPos = manualSlicePoints[i].startPos;
				range->sampleHolder.endPos = (i == numManualSlice - 1) ? waveformBasicNavigator.sample->lengthInSamples
				                                                       : this->manualSlicePoints[i + 1].startPos;
				range->sampleHolder.transpose = manualSlicePoints[i].transpose;
			}
		}
	}

	else if (b == BACK) {
		if (inCardRoutine) {
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		if (slicerMode == SLICER_MODE_MANUAL) {
			uint8_t myImage[displayHeight][displayWidth + sideBarWidth][3];
			waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
			                                  waveformBasicNavigator.xZoom, PadLEDs::image,
			                                  &waveformBasicNavigator.renderData);
			((Kit*)currentSong->currentClip->output)->firstDrum->unassignAllVoices(); //stop
			Kit* kit = (Kit*)currentSong->currentClip->output;
			Drum* drum = kit->firstDrum;
			SoundDrum* soundDrum = (SoundDrum*)drum;
			MultisampleRange* range = (MultisampleRange*)soundDrum->sources[0].getOrCreateFirstRange();
			Sample* sample = (Sample*)range->sampleHolder.audioFile;
			range->sampleHolder.startPos = 0;
			range->sampleHolder.endPos = sample->lengthInSamples;
			range->sampleHolder.transpose = 0;
		}

		numericDriver.setNextTransitionDirection(-1);
		close();
	}
	else {
		return ACTION_RESULT_NOT_DEALT_WITH;
	}

	return ACTION_RESULT_DEALT_WITH;
}

void Slicer::stopAnyPreviewing() {
	Kit* kit = (Kit*)currentSong->currentClip->output;
	SoundDrum* drum = (SoundDrum*)kit->firstDrum;
	drum->unassignAllVoices();
	if (drum->sources[0].ranges.getNumElements()) {
		MultisampleRange* range = (MultisampleRange*)drum->sources[0].ranges.getElement(0);
		range->sampleHolder.setAudioFile(NULL);
	}
}
void Slicer::preview(int64_t startPoint, int64_t endPoint, int transpose, int on) {
	if (on) {
		Kit* kit = (Kit*)currentSong->currentClip->output;
		SoundDrum* drum = (SoundDrum*)kit->firstDrum;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);

		MultisampleRange* range = (MultisampleRange*)drum->sources[0].getOrCreateFirstRange();
		drum->name.set("1");
		drum->sources[0].repeatMode = SAMPLE_REPEAT_ONCE;

		if (!waveformBasicNavigator.sample->filePath.equals(&range->sampleHolder.filePath)) {
			stopAnyPreviewing();
			range->sampleHolder.filePath.set(waveformBasicNavigator.sample->filePath.get());
			range->sampleHolder.loadFile(false, true, true);
		}
		range->sampleHolder.startPos = startPoint;
		if (endPoint != -1)
			range->sampleHolder.endPos = endPoint;
		range->sampleHolder.transpose = transpose;

		ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
		ModelStackWithParamId* modelStackWithParamId =
		    modelStack->addParamCollectionAndId(summary->paramCollection, summary, PARAM_LOCAL_ENV_0_RELEASE);
		ModelStackWithAutoParam* modelStackWithAutoParam =
		    modelStackWithParamId->paramCollection->getAutoParamFromId(modelStackWithParamId);
		modelStackWithAutoParam->autoParam->setCurrentValueWithNoReversionOrRecording(
		    modelStackWithAutoParam, getParamFromUserValue(PARAM_LOCAL_ENV_0_RELEASE, 1));
		modelStackWithParamId =
		    modelStack->addParamCollectionAndId(summary->paramCollection, summary, PARAM_LOCAL_ENV_0_ATTACK);
		modelStackWithAutoParam = modelStackWithParamId->paramCollection->getAutoParamFromId(modelStackWithParamId);
		modelStackWithAutoParam->autoParam->setCurrentValueWithNoReversionOrRecording(
		    modelStackWithAutoParam, getParamFromUserValue(PARAM_LOCAL_ENV_0_ATTACK, 1));
	}
	instrumentClipView.sendAuditionNote(on, 0);
}

int Slicer::padAction(int x, int y, int on) {

	if (on && x < displayWidth && y < displayHeight / 2 && slicerMode == SLICER_MODE_MANUAL) { // pad on

		int slicePadIndex = (x % 4 + (x / 4) * 16) + ((y % 4) * 4); //

		if (slicePadIndex < numManualSlice) { //play slice
			bool closePopup = (currentSlice != slicePadIndex);
			currentSlice = slicePadIndex;
			if (slicePadIndex + 1 < numManualSlice) {
				preview(manualSlicePoints[slicePadIndex].startPos, manualSlicePoints[slicePadIndex + 1].startPos,
				        manualSlicePoints[slicePadIndex].transpose, on);
			}
			else if (slicePadIndex + 1 == numManualSlice) {
				preview(manualSlicePoints[slicePadIndex].startPos, waveformBasicNavigator.sample->lengthInSamples,
				        manualSlicePoints[slicePadIndex].transpose, on);
			}

#if HAVE_OLED
			if (closePopup)
				OLED::removePopup();
#else
			if (closePopup)
				numericDriver.cancelPopup();
#endif
		}
		else { // do slice

			VoiceSample* voiceSample = NULL;
			SamplePlaybackGuide* guide = NULL;
			MultisampleRange* range;
			Kit* kit = (Kit*)currentSong->currentClip->output;
			SoundDrum* drum = (SoundDrum*)kit->firstDrum;

			if (currentSong->currentClip->type == CLIP_TYPE_INSTRUMENT) {
				if (drum->hasAnyVoices()) {
					Voice* assignedVoice = NULL;

					range = (MultisampleRange*)drum->sources[0].getOrCreateFirstRange();

					int ends[2];
					AudioEngine::activeVoices.getRangeForSound(drum, ends);
					for (int v = ends[0]; v < ends[1]; v++) {
						Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
						// Ensure correct MultisampleRange.
						if (thisVoice->guides[0].audioFileHolder != range->getAudioFileHolder()) {
							continue;
						}
						if (!assignedVoice || thisVoice->orderSounded > assignedVoice->orderSounded) {
							assignedVoice = thisVoice;
						}
					}
					if (assignedVoice) {
						VoiceUnisonPartSource* part = &assignedVoice->unisonParts[drum->numUnison >> 1].sources[0];
						if (part && part->active) {
							voiceSample = part->voiceSample;
							guide = &assignedVoice->guides[soundEditor.currentSourceIndex];
						}
					}
				}
			}
			if (voiceSample) {
				int samplePos = voiceSample->getPlaySample((Sample*)range->sampleHolder.audioFile, guide);
				if (samplePos < waveformBasicNavigator.sample->lengthInSamples && numManualSlice < MAX_MANUAL_SLICES) {
					manualSlicePoints[numManualSlice].startPos = samplePos;
					manualSlicePoints[numManualSlice].transpose = 0;

					numManualSlice++;
#if HAVE_OLED
					OLED::removePopup();
#else
					numericDriver.cancelPopup();
#endif

					SliceItem tmp;
					for (int i = 0; i < (numManualSlice - 1); i++) {
						for (int j = (numManualSlice - 1); j > i; j--) {
							if (manualSlicePoints[j].startPos < manualSlicePoints[j - 1].startPos) {
								tmp = manualSlicePoints[j];
								manualSlicePoints[j] = manualSlicePoints[j - 1];
								manualSlicePoints[j - 1] = tmp;
							}
						}
					}
				}
			}
		}

#if HAVE_OLED
		renderUIsForOled();
#else
		redraw();
#endif
		uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
	}
	else if (!on && x < displayWidth && y < displayHeight / 2 && slicerMode == SLICER_MODE_MANUAL) { // pad off
		preview(0, 0, 0, 0);                                                                         //off
	}

	if (slicerMode == SLICER_MODE_MANUAL) {
		return ACTION_RESULT_DEALT_WITH;
	}

	return sampleBrowser.padAction(x, y, on);
}

void Slicer::doSlice() {

	AudioEngine::stopAnyPreviewing();

	int error = sampleBrowser.claimAudioFileForInstrument();
	if (error) {
getOut:
		numericDriver.displayError(error);
		return;
	}

	Kit* kit = (Kit*)currentSong->currentClip->output;

	// Do the first Drum

	// Ensure osc type is "sample"
	if (soundEditor.currentSource->oscType != OSC_TYPE_SAMPLE) {
		soundEditor.currentSound->unassignAllVoices();
		soundEditor.currentSource->setOscType(OSC_TYPE_SAMPLE);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	{
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);
		ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
		ParamSet* paramSet = (ParamSet*)summary->paramCollection;
		int paramId = PARAM_LOCAL_OSC_A_VOLUME + soundEditor.currentSourceIndex;
		ModelStackWithAutoParam* modelStackWithParam =
		    modelStack->addParam(paramSet, summary, paramId, &paramSet->params[paramId]);

		// Reset osc volume, if it's not automated
		if (!modelStackWithParam->autoParam->isAutomated()) {
			modelStackWithParam->autoParam->setCurrentValueWithNoReversionOrRecording(modelStackWithParam, 2147483647);
			//((ParamManagerBase*)soundEditor.currentParamManager)->setPatchedParamValue(PARAM_LOCAL_OSC_A_VOLUME + soundEditor.currentSourceIndex, 2147483647, 0xFFFFFFFF, 0, soundEditor.currentSound, currentSong, currentSong->currentClip, false);
		}

		SoundDrum* firstDrum = (SoundDrum*)soundEditor.currentSound;

		if (firstDrum->nameIsDiscardable) {
			firstDrum->name.set("1");
		}

		MultisampleRange* firstRange = (MultisampleRange*)firstDrum->sources[0].getOrCreateFirstRange();

		Sample* sample = (Sample*)firstRange->sampleHolder.audioFile;

		uint32_t lengthInSamples = sample->lengthInSamples;

		uint32_t lengthSamplesPerSlice = lengthInSamples / numClips;
		uint32_t lengthMSPerSlice = lengthSamplesPerSlice * 1000 / sample->sampleRate;

		bool doEnvelopes =
		    (lengthMSPerSlice >= 90); // Only do fades in and out if we've got at least 100ms to play with

		firstRange->sampleHolder.startPos = 0;
		uint32_t nextDrumStart = lengthInSamples / numClips;
		firstRange->sampleHolder.endPos = nextDrumStart;

		firstDrum->sources[0].repeatMode = (lengthMSPerSlice < 2002) ? SAMPLE_REPEAT_ONCE : SAMPLE_REPEAT_CUT;

		firstDrum->sources[0].sampleControls.reversed = false;

#if 1 || ALPHA_OR_BETA_VERSION
		if (!firstRange->sampleHolder.audioFile) {
			numericDriver.freezeWithError("i032"); // Trying to narrow down E368 that Kevin F got
		}
#endif

		firstRange->sampleHolder.claimClusterReasons(firstDrum->sources[0].sampleControls.reversed, CLUSTER_ENQUEUE);
		if (doEnvelopes) {
			ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
			ModelStackWithParamId* modelStackWithParamId =
			    modelStack->addParamCollectionAndId(summary->paramCollection, summary, PARAM_LOCAL_ENV_0_RELEASE);
			ModelStackWithAutoParam* modelStackWithAutoParam =
			    modelStackWithParamId->paramCollection->getAutoParamFromId(modelStackWithParamId);
			modelStackWithAutoParam->autoParam->setCurrentValueWithNoReversionOrRecording(
			    modelStackWithAutoParam, getParamFromUserValue(PARAM_LOCAL_ENV_0_RELEASE, 1));
		}

		// Do the rest of the Drums
		for (int i = 1; i < numClips; i++) {

			// Make the Drum and its ParamManager
			ParamManagerForTimeline paramManager;
			error = paramManager.setupWithPatching();
			if (error) {
				goto getOut;
			}

			void* drumMemory = generalMemoryAllocator.alloc(sizeof(SoundDrum), NULL, false, true);
			if (!drumMemory) {
ramError:
				error = ERROR_INSUFFICIENT_RAM;
				goto getOut;
			}

			SoundDrum* newDrum = new (drumMemory) SoundDrum();

			MultisampleRange* range = (MultisampleRange*)newDrum->sources[0].getOrCreateFirstRange();
			if (!range) {
ramError2:
				newDrum->~Drum();
				generalMemoryAllocator.dealloc(drumMemory);
				goto ramError;
			}

			char newName[5];
			intToString(i + 1, newName);
			error = newDrum->name.set(newName);
			if (error) {
				goto ramError2;
			}

			Sound::initParams(&paramManager);

			kit->addDrum(newDrum);
			newDrum->setupAsSample(&paramManager);

			range->sampleHolder.startPos = nextDrumStart;
			nextDrumStart = (uint64_t)lengthInSamples * (i + 1) / numClips;
			range->sampleHolder.endPos = nextDrumStart;

			newDrum->sources[0].repeatMode = (lengthMSPerSlice < 2002) ? SAMPLE_REPEAT_ONCE : SAMPLE_REPEAT_CUT;

			range->sampleHolder.filePath.set(&sample->filePath);
			range->sampleHolder.loadFile(false, false, true);

			if (doEnvelopes) {
				paramManager.getPatchedParamSet()->params[PARAM_LOCAL_ENV_0_ATTACK].setCurrentValueBasicForSetup(
				    getParamFromUserValue(PARAM_LOCAL_ENV_0_ATTACK, 1));
				if (i != numClips - 1) {
					paramManager.getPatchedParamSet()->params[PARAM_LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
					    getParamFromUserValue(PARAM_LOCAL_ENV_0_RELEASE, 1));
				}
			}

			currentSong->backUpParamManager(newDrum, currentSong->currentClip, &paramManager,
			                                true); // I moved this here, from being earlier/above. Is this fine?
		}

		// Make NoteRows for all these new Drums
		((Kit*)currentSong->currentClip->output)->resetDrumTempValues();
		firstDrum->noteRowAssignedTemp = 1;
	}
	ModelStackWithTimelineCounter* modelStack = (ModelStackWithTimelineCounter*)modelStackMemory;
	((InstrumentClip*)currentSong->currentClip)->assignDrumsToNoteRows(modelStack);

	((Instrument*)currentSong->currentClip->output)->beenEdited();

	// New NoteRows have probably been created, whose colours haven't been grabbed yet.
	instrumentClipView.recalculateColours();

	numericDriver.setNextTransitionDirection(-1);
	sampleBrowser.exitAndNeverDeleteDrum();
	uiNeedsRendering(&instrumentClipView);
}
