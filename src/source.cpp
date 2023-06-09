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

#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <Cluster.h>
#include <samplebrowser.h>
#include <sound.h>
#include "source.h"
#include "uart.h"
#include "functions.h"
#include "storagemanager.h"
#include "Sample.h"
#include "numericdriver.h"
#include <string.h>
#include "WaveTable.h"
#include "MultisampleRange.h"
#include "View.h"
#include "soundeditor.h"
#include "MultiWaveTableRange.h"

Source::Source() {

	transpose = 0;
	cents = 0;
	repeatMode = SAMPLE_REPEAT_CUT;

	// Synth stuff
	oscType = OSC_TYPE_SQUARE;

	timeStretchAmount = 0;

	defaultRangeI = -1;
}

Source::~Source() {
	destructAllMultiRanges();
}

// Destructs the actual MultiRanges, but doesn't actually deallocate the memory, aka calling empty() on the Array - the caller must do this.
void Source::destructAllMultiRanges() {
	for (int e = 0; e < ranges.getNumElements(); e++) {
		AudioEngine::logAction("destructAllMultiRanges()");
		AudioEngine::routineWithClusterLoading(); // -----------------------------------
		ranges.getElement(e)->~MultiRange();
	}
}

// Only to be called if already determined that oscType == OSC_TYPE_SAMPLE
int32_t Source::getLengthInSamplesAtSystemSampleRate(int note, bool forTimeStretching) {
	MultiRange* range = getRange(note);
	if (range) {
		return ((SampleHolder*)range->getAudioFileHolder())->getLengthInSamplesAtSystemSampleRate(forTimeStretching);
	}
	else return 1; // Why did I put 1?
}

void Source::setCents(int newCents) {
	cents = newCents;
	recalculateFineTuner();
}

void Source::recalculateFineTuner() {
	fineTuner.setup((int32_t)cents * 42949672);
}

// This function has to give the same result as Sound::renderingVoicesInStereo(). The duplication is for optimization.
bool Source::renderInStereo(SampleHolder* sampleHolder) {
	if (!AudioEngine::renderInStereo) return false;

	return (oscType == OSC_TYPE_SAMPLE && sampleHolder && sampleHolder->audioFile
	        && sampleHolder->audioFile->numChannels == 2)
	       || (oscType == OSC_TYPE_INPUT_STEREO && (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn));
}

void Source::detachAllAudioFiles() {
	for (int e = 0; e < ranges.getNumElements(); e++) {
		if (!(e & 7))
			AudioEngine::
			    routineWithClusterLoading(); // --------------------------------------- // 7 works, 15 occasionally drops voices - for multisampled synths
		ranges.getElement(e)->getAudioFileHolder()->setAudioFile(NULL);
	}
}

int Source::loadAllSamples(bool mayActuallyReadFiles) {
	for (int e = 0; e < ranges.getNumElements(); e++) {
		AudioEngine::logAction("Source::loadAllSamples");
		if (!(e & 3))
			AudioEngine::
			    routineWithClusterLoading(); // -------------------------------------- // 3 works, 7 occasionally drops voices - for multisampled synths
		if (mayActuallyReadFiles && shouldAbortLoading()) return ERROR_ABORTED_BY_USER;
		ranges.getElement(e)->getAudioFileHolder()->loadFile(sampleControls.reversed, false, mayActuallyReadFiles,
		                                                     CLUSTER_ENQUEUE, 0, true);
	}

	return NO_ERROR;
}

// Only to be called if already determined that oscType == OSC_TYPE_SAMPLE
void Source::setReversed(bool newReversed) {
	sampleControls.reversed = newReversed;
	for (int e = 0; e < ranges.getNumElements(); e++) {
		MultiRange* range = (MultisampleRange*)ranges.getElement(e);
		SampleHolder* holder = (SampleHolder*)range->getAudioFileHolder();
		Sample* sample = (Sample*)holder->audioFile;
		if (sample) {
			if (sampleControls.reversed && holder->endPos > sample->lengthInSamples) {
				holder->endPos = sample->lengthInSamples;
			}
			holder->claimClusterReasons(sampleControls.reversed);
		}
	}
}

MultiRange* Source::getRange(int note) {
	if (ranges.getNumElements() == 1) return ranges.getElement(0);
	else if (ranges.getNumElements() == 0) return NULL;
	else {
		defaultRangeI = ranges.search(note, GREATER_OR_EQUAL);
		if (defaultRangeI == ranges.getNumElements()) defaultRangeI--;
		return ranges.getElement(defaultRangeI);
	}
}

int Source::getRangeIndex(int note) {
	if (ranges.getNumElements() <= 1) return 0;
	else {
		int e = ranges.search(note, GREATER_OR_EQUAL);
		if (e == ranges.getNumElements()) e--;
		return e;
	}
}

MultiRange* Source::getOrCreateFirstRange() {
	if (!ranges.getNumElements()) {
		MultiRange* newRange = ranges.insertMultiRange(
		    0); // Default option - allowed e.g. for a new Sound where the current process is the Ranges get set up before oscType is switched over to SAMPLE - but this can't happen for WAVETABLE so that's ok
		if (!newRange) return NULL;

		newRange->topNote = 32767;
		return newRange;
	}

	else {
		return ranges.getElement(0);
	}
}

bool Source::hasAtLeastOneAudioFileLoaded() {
	for (int e = 0; e < ranges.getNumElements(); e++) {
		if (ranges.getElement(e)->getAudioFileHolder()->audioFile) return true;
	}
	return false;
}

void Source::doneReadingFromFile(Sound* sound) {

	int synthMode = sound->getSynthMode();

	if (synthMode == SYNTH_MODE_FM) oscType = OSC_TYPE_SINE;
	else if (synthMode == SYNTH_MODE_RINGMOD) oscType = getMin((int)oscType, NUM_OSC_TYPES_RINGMODDABLE - 1);

	bool isActualSampleOscillator = (synthMode != SYNTH_MODE_FM && oscType == OSC_TYPE_SAMPLE);

	if (oscType == OSC_TYPE_SAMPLE) {
		for (int e = 0; e < ranges.getNumElements(); e++) {
			MultisampleRange* range = (MultisampleRange*)ranges.getElement(e);
			if (isActualSampleOscillator) {
				range->sampleHolder.transpose += transpose;
				range->sampleHolder.setCents(range->sampleHolder.cents + cents);
			}
			else {
				range->sampleHolder.recalculateFineTuner();
			}
		}
	}

	if (isActualSampleOscillator) {
		transpose = 0;
		setCents(0);
	}

	else {
		recalculateFineTuner();
	}
}

// Only to be called if already determined that oscType == OSC_TYPE_SAMPLE
bool Source::hasAnyLoopEndPoint() {
	for (int e = 0; e < ranges.getNumElements(); e++) {
		MultisampleRange* range = (MultisampleRange*)ranges.getElement(e);
		if (range->sampleHolder.loopEndPos) return true;
	}
	return false;
}

// If setting to SAMPLE or WAVETABLE, you must call unassignAllVoices before this, because ranges is going to get emptied.
void Source::setOscType(int newType) {

	int multiRangeSize;
	if (newType == OSC_TYPE_SAMPLE) {
		multiRangeSize = sizeof(MultisampleRange);
possiblyDeleteRanges:
		if (ranges.elementSize != multiRangeSize) {

			/*
			if (ranges.anyRangeHasAudioFile()) {

			}

			destructAllMultiRanges();
			*/

doChangeType:
			int error = ranges.changeType(multiRangeSize);
			if (error) {
				destructAllMultiRanges();
				ranges.empty();
				soundEditor.currentMultiRangeIndex = 0;
				goto doChangeType; // Can't fail now it's empty.
			}

			oscType = newType;

			getOrCreateFirstRange(); // Ensure there's at least 1. If this returns NULL and we're in the SoundEditor or something, we're screwed.

			if (soundEditor.currentMultiRangeIndex >= 0
			    && soundEditor.currentMultiRangeIndex < ranges.getNumElements()) {
				soundEditor.currentMultiRange =
				    (MultiRange*)ranges.getElementAddress(soundEditor.currentMultiRangeIndex);
			}
		}
	}
	else if (newType == OSC_TYPE_WAVETABLE) {
		multiRangeSize = sizeof(MultiWaveTableRange);
		goto possiblyDeleteRanges;
	}

	oscType = newType;
}

/*
	for (int e = 0; e < ranges.getNumElements(); e++) {
		ranges.getElement(e)->
	}

 */
