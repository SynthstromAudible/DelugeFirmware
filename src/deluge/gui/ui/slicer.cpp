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

#include "gui/ui/slicer.h"
#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/waveform/waveform_basic_navigator.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "model/voice/voice.h"
#include "model/voice/voice_sample.h"
#include "model/voice/voice_vector.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"
#include "util/functions.h"
#include <string.h>

using namespace deluge::gui;

Slicer slicer{};

namespace params = deluge::modulation::params;

void Slicer::focusRegained() {

	actionLogger.deleteAllLogs();

	numClips = 16;

	numManualSlice = 1;
	currentSlice = 0;
	slicerMode = SLICER_MODE_REGION;
	for (int32_t i = 0; i < MAX_MANUAL_SLICES; i++) {
		manualSlicePoints[i].startPos = 0;
		manualSlicePoints[i].transpose = 0;
	}

	if (display->have7SEG()) {
		redraw();
	}
}

void Slicer::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {

	int32_t windowWidth = 100;
	int32_t windowHeight = 31;
	int32_t horizontalShift = 6;

	int32_t windowMinX = (OLED_MAIN_WIDTH_PIXELS - windowWidth) >> 1;
	windowMinX += horizontalShift;
	int32_t windowMaxX = windowMinX + windowWidth;

	int32_t windowMinY = (OLED_MAIN_HEIGHT_PIXELS - windowHeight) >> 1;
	windowMinY += 2;
	int32_t windowMaxY = windowMinY + windowHeight;

	canvas.clearAreaExact(windowMinX + 1, windowMinY + 1, windowMaxX - 1, windowMaxY - 1);

	canvas.drawRectangle(windowMinX, windowMinY, windowMaxX, windowMaxY);
	canvas.drawHorizontalLine(windowMinY + 15, 26, OLED_MAIN_WIDTH_PIXELS - 22);
	canvas.drawString("Num. slices", 30, windowMinY + 6, kTextSpacingX, kTextSpacingY);

	char buffer[12];
	intToString(slicerMode == SLICER_MODE_REGION ? numClips : numManualSlice, buffer);
	canvas.drawStringCentred(buffer, windowMinY + 18, kTextSpacingX, kTextSpacingY,
	                         (OLED_MAIN_WIDTH_PIXELS >> 1) + horizontalShift);
}

void Slicer::redraw() {
	display->setTextAsNumber(slicerMode == SLICER_MODE_REGION ? numClips : numManualSlice, 255, true);
}

bool Slicer::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {

	if (slicerMode == SLICER_MODE_REGION) {
		RGB myImage[kDisplayHeight][kDisplayWidth + kSideBarWidth];
		waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
		                                  waveformBasicNavigator.xZoom, image, &waveformBasicNavigator.renderData);
	}
	else if (slicerMode == SLICER_MODE_MANUAL) {

		RGB myImage[kDisplayHeight][kDisplayWidth + kSideBarWidth];
		waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
		                                  waveformBasicNavigator.xZoom, myImage, &waveformBasicNavigator.renderData);

		for (int32_t xx = 0; xx < kDisplayWidth; xx++) {
			for (int32_t yy = 0; yy < kDisplayHeight / 2; yy++) {
				image[yy + 4][xx] = RGB::average(myImage[yy * 2][xx], myImage[yy * 2 + 1][xx]);
			}
		}
		for (int32_t i = 0; i < numManualSlice; i++) { // Slices
			int32_t x = manualSlicePoints[i].startPos / (waveformBasicNavigator.sample->lengthInSamples + 0.0) * 16;
			image[4][x] = RGB{
			    1,
			    (i == currentSlice) ? 200_u8 : 16_u8,
			    1,
			};
		}

		for (int32_t i = 0; i < MAX_MANUAL_SLICES; i++) { // Lower screen
			int32_t xx = (i % 4) + (i / 16) * 4;
			int32_t yy = (i / 4) % 4;
			int32_t page = i / 16;

			RGB colour = RGB::monochrome(3);
			size_t dimLevel = (i < numManualSlice) ? 2 : 6;
			if (page % 2 == 0) {
				colour = colours::green.dim(dimLevel);
			}
			else {
				colour = colours::darkblue.dim(dimLevel);
			}
			if (i == this->currentSlice) {
				colour = colours::green.dim();
			}

			image[yy][xx] = colour;
		}
	}
	return true;
}

const uint8_t zeroes[] = {0, 0, 0, 0, 0, 0, 0, 0};

void Slicer::graphicsRoutine() {

	int32_t newTickSquare = 255;
	VoiceSample* voiceSample = NULL;
	SamplePlaybackGuide* guide = NULL;

	MultisampleRange* range;
	Kit* kit = getCurrentKit();
	SoundDrum* drum = (SoundDrum*)kit->firstDrum;

	if (getCurrentClip()->type == ClipType::INSTRUMENT) {

		if (drum->hasAnyVoices()) {

			Voice* assignedVoice = NULL;

			range = (MultisampleRange*)drum->sources[0].getOrCreateFirstRange();

			int32_t ends[2];
			AudioEngine::activeVoices.getRangeForSound(drum, ends);
			for (int32_t v = ends[0]; v < ends[1]; v++) {
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
		int32_t samplePos = voiceSample->getPlaySample((Sample*)range->sampleHolder.audioFile, guide);
		if (samplePos >= waveformBasicNavigator.xScroll) {
			newTickSquare = (samplePos - waveformBasicNavigator.xScroll) / waveformBasicNavigator.xZoom;
			if (newTickSquare >= kDisplayWidth) {
				newTickSquare = 255;
			}
		}
	}

	uint8_t tickSquares[kDisplayHeight];
	memset(tickSquares, 255, kDisplayHeight);
	tickSquares[kDisplayHeight - 1] = newTickSquare;
	tickSquares[kDisplayHeight - 2] = newTickSquare;
	tickSquares[kDisplayHeight - 3] = newTickSquare;
	tickSquares[kDisplayHeight - 4] = newTickSquare;

	PadLEDs::setTickSquares(tickSquares, zeroes);
}

ActionResult Slicer::horizontalEncoderAction(int32_t offset) {

	if (slicerMode == SLICER_MODE_MANUAL) {
		int32_t newPos = manualSlicePoints[currentSlice].startPos;
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

		if (display->haveOLED()) {
			char buffer[24];
			strcpy(buffer, "Start: ");
			intToString(manualSlicePoints[currentSlice].startPos, buffer + strlen(buffer));
			display->popupTextTemporary(buffer);
		}
		else {
			char buffer[12];
			strcpy(buffer, "");
			intToString(manualSlicePoints[currentSlice].startPos / 1000, buffer + strlen(buffer));
			display->displayPopup(buffer, 0, true);
		}
		uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
	}
	return ActionResult::DEALT_WITH;
}

ActionResult Slicer::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (slicerMode == SLICER_MODE_MANUAL) {

		manualSlicePoints[currentSlice].transpose += offset;
		if (manualSlicePoints[currentSlice].transpose > 24)
			manualSlicePoints[currentSlice].transpose = 24;
		if (manualSlicePoints[currentSlice].transpose < -24)
			manualSlicePoints[currentSlice].transpose = -24;
		if (display->haveOLED()) {
			char buffer[32];
			snprintf(buffer, 32, "Transpose: %d", manualSlicePoints[currentSlice].transpose);
			display->popupTextTemporary(buffer);
		}
		else {
			char buffer[12];
			strcpy(buffer, "");
			intToString(manualSlicePoints[currentSlice].transpose, buffer + strlen(buffer));
			display->displayPopup(buffer, 0, true);
		}
	}
	return ActionResult::DEALT_WITH;
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
	else { // SLICER_MODE_MANUAL
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

	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		redraw();
	}
}

ActionResult Slicer::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (currentUIMode != UI_MODE_NONE || !on) {
		return ActionResult::NOT_DEALT_WITH;
	}

	// switch slicer mode
	if (b == X_ENC && on) {
		slicerMode++;
		slicerMode %= 2;
		if (slicerMode == SLICER_MODE_MANUAL)
			AudioEngine::stopAnyPreviewing();
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			redraw();
		}

		getCurrentKit()->firstDrum->unassignAllVoices(); // stop
		uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
		return ActionResult::DEALT_WITH;
	}

	// pop up Transpose value
	if (b == Y_ENC && on && slicerMode == SLICER_MODE_MANUAL && currentSlice < numManualSlice) {
		if (display->haveOLED()) {

			char buffer[24];
			strcpy(buffer, "Transpose: ");
			intToString(manualSlicePoints[currentSlice].transpose, buffer + strlen(buffer));
			display->popupTextTemporary(buffer);
		}
		else {
			char buffer[12];
			strcpy(buffer, "");
			intToString(manualSlicePoints[currentSlice].transpose, buffer + strlen(buffer));
			display->displayPopup(buffer, 0, true);
		}
		return ActionResult::DEALT_WITH;
	}

	// delete slice
	if (b == SAVE && on && slicerMode == SLICER_MODE_MANUAL) {
		int32_t xx = (currentSlice % 4) + (currentSlice / 16) * 4;
		int32_t yy = (currentSlice / 4) % 4;
		if (matrixDriver.isPadPressed(xx, yy) && currentSlice < numManualSlice) {
			int32_t target = currentSlice;

			for (int32_t i = 0; i < MAX_MANUAL_SLICES - 1; i++) {
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
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			else {
				redraw();
			}
			return ActionResult::DEALT_WITH;
		}
	}

	if (b == SELECT_ENC) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		if (slicerMode == SLICER_MODE_REGION) {
			doSlice();
		}
		else {
			getCurrentKit()->firstDrum->unassignAllVoices(); // stop
			numClips = numManualSlice;
			doSlice();
			Kit* kit = getCurrentKit();
			for (int32_t i = 0; i < numManualSlice; i++) {
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
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		if (slicerMode == SLICER_MODE_MANUAL) {
			RGB myImage[kDisplayHeight][kDisplayWidth + kSideBarWidth];
			waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
			                                  waveformBasicNavigator.xZoom, PadLEDs::image,
			                                  &waveformBasicNavigator.renderData);
			getCurrentKit()->firstDrum->unassignAllVoices(); // stop
			Kit* kit = getCurrentKit();
			Drum* drum = kit->firstDrum;
			SoundDrum* soundDrum = (SoundDrum*)drum;
			MultisampleRange* range = (MultisampleRange*)soundDrum->sources[0].getOrCreateFirstRange();
			Sample* sample = (Sample*)range->sampleHolder.audioFile;
			range->sampleHolder.startPos = 0;
			range->sampleHolder.endPos = sample->lengthInSamples;
			range->sampleHolder.transpose = 0;
		}

		display->setNextTransitionDirection(-1);
		close();
	}
	else {
		return ActionResult::NOT_DEALT_WITH;
	}

	return ActionResult::DEALT_WITH;
}

void Slicer::stopAnyPreviewing() {
	Kit* kit = getCurrentKit();
	SoundDrum* drum = (SoundDrum*)kit->firstDrum;
	drum->unassignAllVoices();
	if (drum->sources[0].ranges.getNumElements()) {
		MultisampleRange* range = (MultisampleRange*)drum->sources[0].ranges.getElement(0);
		range->sampleHolder.setAudioFile(NULL);
	}
}
void Slicer::preview(int64_t startPoint, int64_t endPoint, int32_t transpose, int32_t on) {
	if (on) {
		Kit* kit = getCurrentKit();
		SoundDrum* drum = (SoundDrum*)kit->firstDrum;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);

		MultisampleRange* range = (MultisampleRange*)drum->sources[0].getOrCreateFirstRange();
		drum->name.set("1");
		drum->sources[0].repeatMode = SampleRepeatMode::ONCE;

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
		    modelStack->addParamCollectionAndId(summary->paramCollection, summary, params::LOCAL_ENV_0_RELEASE);
		ModelStackWithAutoParam* modelStackWithAutoParam =
		    modelStackWithParamId->paramCollection->getAutoParamFromId(modelStackWithParamId);
		modelStackWithAutoParam->autoParam->setCurrentValueWithNoReversionOrRecording(
		    modelStackWithAutoParam, getParamFromUserValue(params::LOCAL_ENV_0_RELEASE, 1));
		modelStackWithParamId =
		    modelStack->addParamCollectionAndId(summary->paramCollection, summary, params::LOCAL_ENV_0_ATTACK);
		modelStackWithAutoParam = modelStackWithParamId->paramCollection->getAutoParamFromId(modelStackWithParamId);
		modelStackWithAutoParam->autoParam->setCurrentValueWithNoReversionOrRecording(
		    modelStackWithAutoParam, getParamFromUserValue(params::LOCAL_ENV_0_ATTACK, 1));
	}
	instrumentClipView.sendAuditionNote(on, 0, 64, 0);
}

ActionResult Slicer::padAction(int32_t x, int32_t y, int32_t on) {

	if (on && x < kDisplayWidth && y < kDisplayHeight / 2 && slicerMode == SLICER_MODE_MANUAL) { // pad on

		int32_t slicePadIndex = (x % 4 + (x / 4) * 16) + ((y % 4) * 4); //

		if (slicePadIndex < numManualSlice) { // play slice
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

			if (closePopup) {
				display->cancelPopup();
			}
		}
		else { // do slice

			VoiceSample* voiceSample = NULL;
			SamplePlaybackGuide* guide = NULL;
			MultisampleRange* range;
			Kit* kit = getCurrentKit();
			SoundDrum* drum = (SoundDrum*)kit->firstDrum;

			if (getCurrentClip()->type == ClipType::INSTRUMENT) {
				if (drum->hasAnyVoices()) {
					Voice* assignedVoice = NULL;

					range = (MultisampleRange*)drum->sources[0].getOrCreateFirstRange();

					int32_t ends[2];
					AudioEngine::activeVoices.getRangeForSound(drum, ends);
					for (int32_t v = ends[0]; v < ends[1]; v++) {
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
				int32_t samplePos = voiceSample->getPlaySample((Sample*)range->sampleHolder.audioFile, guide);
				if (samplePos < waveformBasicNavigator.sample->lengthInSamples && numManualSlice < MAX_MANUAL_SLICES) {
					manualSlicePoints[numManualSlice].startPos = samplePos;
					manualSlicePoints[numManualSlice].transpose = 0;

					numManualSlice++;
					display->cancelPopup();

					SliceItem tmp;
					for (int32_t i = 0; i < (numManualSlice - 1); i++) {
						for (int32_t j = (numManualSlice - 1); j > i; j--) {
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

		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			redraw();
		}
		uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
	}
	else if (!on && x < kDisplayWidth && y < kDisplayHeight / 2 && slicerMode == SLICER_MODE_MANUAL) { // pad off
		preview(0, 0, 0, 0);                                                                           // off
	}

	if (slicerMode == SLICER_MODE_MANUAL) {
		return ActionResult::DEALT_WITH;
	}

	return sampleBrowser.padAction(x, y, on);
}

void Slicer::doSlice() {

	AudioEngine::stopAnyPreviewing();

	Error error = sampleBrowser.claimAudioFileForInstrument();
	if (error != Error::NONE) {
getOut:
		display->displayError(error);
		return;
	}

	Kit* kit = getCurrentKit();

	// Do the first Drum

	// Ensure osc type is "sample"
	if (soundEditor.currentSource->oscType != OscType::SAMPLE) {
		soundEditor.currentSound->unassignAllVoices();
		soundEditor.currentSource->setOscType(OscType::SAMPLE);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	{
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);
		ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
		ParamSet* paramSet = (ParamSet*)summary->paramCollection;
		int32_t paramId = params::LOCAL_OSC_A_VOLUME + soundEditor.currentSourceIndex;
		ModelStackWithAutoParam* modelStackWithParam =
		    modelStack->addParam(paramSet, summary, paramId, &paramSet->params[paramId]);

		// Reset osc volume, if it's not automated
		if (!modelStackWithParam->autoParam->isAutomated()) {
			modelStackWithParam->autoParam->setCurrentValueWithNoReversionOrRecording(modelStackWithParam, 2147483647);
			//((ParamManagerBase*)soundEditor.currentParamManager)->setPatchedParamValue(params::LOCAL_OSC_A_VOLUME +
			// soundEditor.currentSourceIndex, 2147483647, 0xFFFFFFFF, 0, soundEditor.currentSound, currentSong,
			// getCurrentClip(), false);
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

		firstDrum->sources[0].repeatMode =
		    (lengthMSPerSlice < 2002) ? SampleRepeatMode::ONCE : FlashStorage::defaultSliceMode;

		firstDrum->sources[0].sampleControls.reversed = false;
		firstDrum->sources[0].sampleControls.invertReversed = false;

#if 1 || ALPHA_OR_BETA_VERSION
		if (!firstRange->sampleHolder.audioFile) {
			FREEZE_WITH_ERROR("i032"); // Trying to narrow down E368 that Kevin F got
		}
#endif

		firstRange->sampleHolder.claimClusterReasons(firstDrum->sources[0].sampleControls.isCurrentlyReversed(),
		                                             CLUSTER_ENQUEUE);
		if (doEnvelopes) {
			ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
			ModelStackWithParamId* modelStackWithParamId =
			    modelStack->addParamCollectionAndId(summary->paramCollection, summary, params::LOCAL_ENV_0_RELEASE);
			ModelStackWithAutoParam* modelStackWithAutoParam =
			    modelStackWithParamId->paramCollection->getAutoParamFromId(modelStackWithParamId);
			modelStackWithAutoParam->autoParam->setCurrentValueWithNoReversionOrRecording(
			    modelStackWithAutoParam, getParamFromUserValue(params::LOCAL_ENV_0_RELEASE, 1));
		}

		// Do the rest of the Drums
		for (int32_t i = 1; i < numClips; i++) {

			// Make the Drum and its ParamManager
			ParamManagerForTimeline paramManager;
			error = paramManager.setupWithPatching();
			if (error != Error::NONE) {
				goto getOut;
			}

			void* drumMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(SoundDrum));
			if (!drumMemory) {
ramError:
				error = Error::INSUFFICIENT_RAM;
				goto getOut;
			}

			SoundDrum* newDrum = new (drumMemory) SoundDrum();

			MultisampleRange* range = (MultisampleRange*)newDrum->sources[0].getOrCreateFirstRange();
			if (!range) {
ramError2:
				newDrum->~SoundDrum();
				delugeDealloc(drumMemory);
				goto ramError;
			}

			char newName[5];
			intToString(i + 1, newName);
			error = newDrum->name.set(newName);
			if (error != Error::NONE) {
				goto ramError2;
			}

			Sound::initParams(&paramManager);

			kit->addDrum(newDrum);
			newDrum->setupAsSample(&paramManager);

			range->sampleHolder.startPos = nextDrumStart;
			nextDrumStart = (uint64_t)lengthInSamples * (i + 1) / numClips;
			range->sampleHolder.endPos = nextDrumStart;

			newDrum->sources[0].repeatMode =
			    (lengthMSPerSlice < 2002) ? SampleRepeatMode::ONCE : FlashStorage::defaultSliceMode;

			range->sampleHolder.filePath.set(&sample->filePath);
			range->sampleHolder.loadFile(false, false, true);

			if (doEnvelopes) {
				paramManager.getPatchedParamSet()->params[params::LOCAL_ENV_0_ATTACK].setCurrentValueBasicForSetup(
				    getParamFromUserValue(params::LOCAL_ENV_0_ATTACK, 1));
				if (i != numClips - 1) {
					paramManager.getPatchedParamSet()->params[params::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
					    getParamFromUserValue(params::LOCAL_ENV_0_RELEASE, 1));
				}
			}

			// I moved this here, from being earlier/above. Is this fine?
			currentSong->backUpParamManager(newDrum, getCurrentClip(), &paramManager, true);
		}

		// Make NoteRows for all these new Drums
		getCurrentKit()->resetDrumTempValues();
		firstDrum->noteRowAssignedTemp = 1;
	}
	ModelStackWithTimelineCounter* modelStack = (ModelStackWithTimelineCounter*)modelStackMemory;
	getCurrentInstrumentClip()->assignDrumsToNoteRows(modelStack);

	getCurrentInstrument()->beenEdited();

	// New NoteRows have probably been created, whose colours haven't been grabbed yet.
	instrumentClipView.recalculateColours();

	display->setNextTransitionDirection(-1);
	sampleBrowser.exitAndNeverDeleteDrum();
	uiNeedsRendering(&instrumentClipView);
}
