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

#include <AudioEngine.h>
#include <ContextMenuSampleBrowserKit.h>
#include <InstrumentClip.h>
#include <InstrumentClipView.h>
#include <ParamManager.h>
#include <samplebrowser.h>
#include <sound.h>
#include <sounddrum.h>
#include "Slicer.h"
#include "functions.h"
#include "numericdriver.h"
#include "Sample.h"
#include "song.h"
#include "soundeditor.h"
#include "kit.h"
#include <new>
#include <string.h>
#include "GeneralMemoryAllocator.h"
#include "ActionLogger.h"
#include "MultisampleRange.h"
#include "ModelStack.h"
#include "ParamSet.h"
#include "oled.h"

extern "C" {
#include "cfunctions.h"
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
	intToString(numClips, buffer);
	OLED::drawStringCentred(buffer, windowMinY + 18, image[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X, TEXT_SPACING_Y,
	                        (OLED_MAIN_WIDTH_PIXELS >> 1) + horizontalShift);
}

#else

void Slicer::redraw() {
	numericDriver.setTextAsNumber(numClips, 255, true);
}
#endif

void Slicer::selectEncoderAction(int8_t offset) {
	numClips += offset;
	if (numClips == 257) numClips = 2;
	else if (numClips == 1) numClips = 256;
#if HAVE_OLED
	renderUIsForOled();
#else
	redraw();
#endif
}

int Slicer::buttonAction(int x, int y, bool on, bool inCardRoutine) {
	if (currentUIMode != UI_MODE_NONE || !on) return ACTION_RESULT_NOT_DEALT_WITH;

	if (x == selectEncButtonX && y == selectEncButtonY) {
		if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		doSlice();
	}

	else if (x == backButtonX && y == backButtonY) {
		if (inCardRoutine) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		numericDriver.setNextTransitionDirection(-1);
		close();
	}
	else return ACTION_RESULT_NOT_DEALT_WITH;

	return ACTION_RESULT_DEALT_WITH;
}

int Slicer::padAction(int x, int y, int on) {
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
		if (!firstRange->sampleHolder.audioFile)
			numericDriver.freezeWithError("i032"); // Trying to narrow down E368 that Kevin F got
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
			if (error) goto getOut;

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
			if (error) goto ramError2;

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
