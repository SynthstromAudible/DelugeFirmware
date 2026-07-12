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

#include "processing/source.h"
#include "definitions_cxx.hpp"
#include "deluge-project/doc_source.h"
#include "dsp/dx/engine.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "model/sample/sample.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multi_wave_table_range.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/wave_table/wave_table.h"
#include "util/functions.h"
#include <cstring>
#include <iterator>

Source::Source() {

	transpose = 0;
	cents = 0;
	repeatMode = SampleRepeatMode::CUT;

	// Synth stuff
	oscType = OscType::SQUARE;

	timeStretchAmount = 0;

	defaultRangeI = -1;
	dxPatch = nullptr;
}

Source::~Source() {
}

void Source::bindDocSource(deluge::project::DocSource* d) {
	docSource_ = d;
	seedDocSourceScalars();
	seedDocDxPatch();
	seedDocRanges();
}

// Copy the DOM-owned scalars native->record (guarded). Load-bridge: run at bind and re-run at doneReadingFromFile (a
// SoundDrum binds before its fields are parsed, so the bind-time seed is stale for drums). dxPatch + ranges are later
// slices — not seeded here.
void Source::seedDocSourceScalars() {
	if (docSource_ == nullptr) {
		return;
	}
	deluge::project::DocSource& d = *docSource_;
	d.osc_type = static_cast<uint8_t>(oscType);
	d.transpose = transpose;
	d.cents = cents;
	d.repeat_mode = static_cast<uint8_t>(repeatMode);
	d.time_stretch_amount = timeStretchAmount;
	d.interpolation_mode = static_cast<uint8_t>(sampleControls.interpolationMode);
	d.pitch_and_speed_independent = sampleControls.pitchAndSpeedAreIndependent;
	d.reversed = sampleControls.reversed;
}

void Source::seedDocDxPatch() {
	if (docSource_ == nullptr) {
		return;
	}
	if (dxPatch != nullptr) {
		deluge::project::DocDxPatch p;
		for (int32_t b = 0; b < 156; b++) {
			p.params[b] = dxPatch->params[b];
		}
		p.engine_mode = dxPatch->engineMode;
		p.random_detune = dxPatch->random_detune;
		docSource_->dx_patch = p;
	}
	else {
		docSource_->dx_patch.reset();
	}
}

void Source::seedDocRanges() {
	if (docSource_ == nullptr) {
		return;
	}
	docSource_->ranges.clear();
	int32_t numRanges = std::ssize(ranges);
	docSource_->ranges.reserve(numRanges);
	for (int32_t e = 0; e < numRanges; e++) {
		MultisampleRange* range = (MultisampleRange*)ranges.getElement(e);
		deluge::project::DocRange dr;
		dr.top_note = range->topNote;
		dr.file_path =
		    range->sampleHolder.audioFile ? range->sampleHolder.audioFile->filePath : range->sampleHolder.filePath;
		if (oscType == OscType::SAMPLE) {
			dr.start_pos = range->sampleHolder.startPos;
			dr.end_pos = range->sampleHolder.endPos;
			dr.loop_start_pos = range->sampleHolder.loopStartPos;
			dr.loop_end_pos = range->sampleHolder.loopEndPos;
			dr.transpose = range->sampleHolder.transpose;
			dr.cents = range->sampleHolder.cents;
		}
		docSource_->ranges.push_back(std::move(dr));
	}
}

void Source::setTranspose(int16_t newTranspose) {
	transpose = newTranspose;
	if (docSource_ != nullptr) {
		docSource_->transpose = newTranspose;
	}
}

void Source::setRepeatMode(SampleRepeatMode newMode) {
	repeatMode = newMode;
	if (docSource_ != nullptr) {
		docSource_->repeat_mode = static_cast<uint8_t>(newMode);
	}
}

void Source::setTimeStretchAmount(int8_t newAmount) {
	timeStretchAmount = newAmount;
	if (docSource_ != nullptr) {
		docSource_->time_stretch_amount = newAmount;
	}
}

void Source::setInterpolationMode(InterpolationMode newMode) {
	sampleControls.interpolationMode = newMode;
	if (docSource_ != nullptr) {
		docSource_->interpolation_mode = static_cast<uint8_t>(newMode);
	}
}

void Source::setPitchAndSpeedIndependent(bool independent) {
	sampleControls.pitchAndSpeedAreIndependent = independent;
	if (docSource_ != nullptr) {
		docSource_->pitch_and_speed_independent = independent;
	}
}

// Only to be called if already determined that oscType == OscType::SAMPLE
int32_t Source::getLengthInSamplesAtSystemSampleRate(int32_t note, bool forTimeStretching) {
	MultiRange* range = getRange(note);
	if (range != nullptr) {
		return ((SampleHolder*)range->getAudioFileHolder())->getLengthInSamplesAtSystemSampleRate(forTimeStretching);
	}
	return 1; // Why did I put 1?
}

void Source::setCents(int32_t newCents) {
	cents = newCents;
	if (docSource_ != nullptr) {
		docSource_->cents = newCents;
	}
	recalculateFineTuner();
}

void Source::recalculateFineTuner() {
	fineTuner.setup((int32_t)cents * 42949672);
}

// This function has to give the same result as Sound::renderingVoicesInStereo(). The duplication is for optimization.
bool Source::renderInStereo(Sound* s, SampleHolder* sampleHolder) {
	if (!AudioEngine::renderInStereo) {
		return false;
	}

	if (s->unisonStereoSpread && s->numUnison > 1) {
		return true;
	}

	return (oscType == OscType::SAMPLE && sampleHolder && sampleHolder->audioFile
	        && sampleHolder->audioFile->numChannels == 2)
	       || (oscType == OscType::INPUT_STEREO && (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn));
}

void Source::detachAllAudioFiles() {
	for (int32_t e = 0; e < std::ssize(ranges); e++) {
		if (!(e & 7)) { // 7 works, 15 occasionally drops voices - for multisampled synths
			AudioEngine::routineWithClusterLoading();
		}
		ranges.getElement(e)->getAudioFileHolder()->setAudioFile(nullptr);
	}
}

Error Source::loadAllSamples(bool mayActuallyReadFiles) {
	for (int32_t e = 0; e < std::ssize(ranges); e++) {
		AudioEngine::logAction("Source::loadAllSamples");
		if (!(e & 3)) { // 3 works, 7 occasionally drops voices - for multisampled synths
			AudioEngine::routineWithClusterLoading();
		}
		if (mayActuallyReadFiles && shouldAbortLoading()) {
			return Error::ABORTED_BY_USER;
		}
		ranges.getElement(e)->getAudioFileHolder()->loadFile(sampleControls.isCurrentlyReversed(), false,
		                                                     mayActuallyReadFiles, CLUSTER_ENQUEUE, nullptr, true);
	}

	return Error::NONE;
}

// Only to be called if already determined that oscType == OscType::SAMPLE
void Source::setReversed(bool newReversed) {
	sampleControls.reversed = newReversed;
	if (docSource_ != nullptr) {
		docSource_->reversed = newReversed;
	}
	for (int32_t e = 0; e < std::ssize(ranges); e++) {
		MultiRange* range = (MultisampleRange*)ranges.getElement(e);
		SampleHolder* holder = (SampleHolder*)range->getAudioFileHolder();
		Sample* sample = (Sample*)holder->audioFile;
		if (sample) {
			if (sampleControls.isCurrentlyReversed() && holder->endPos > sample->lengthInSamples) {
				holder->endPos = sample->lengthInSamples;
			}
			holder->claimClusterReasons(sampleControls.isCurrentlyReversed());
		}
	}
}

MultiRange* Source::getRange(int32_t note) {
	if (std::ssize(ranges) == 1) {
		return ranges.getElement(0);
	}
	else if (ranges.empty()) {
		return nullptr;
	}
	else {
		defaultRangeI = ranges.firstAtOrAfter(note);
		if (defaultRangeI == std::ssize(ranges)) {
			defaultRangeI--;
		}
		return ranges.getElement(defaultRangeI);
	}
}

int32_t Source::getRangeIndex(int32_t note) {
	if (std::ssize(ranges) <= 1) {
		return 0;
	}
	else {
		int32_t e = ranges.firstAtOrAfter(note);
		if (e == std::ssize(ranges)) {
			e--;
		}
		return e;
	}
}

MultiRange* Source::getOrCreateFirstRange() {
	if (ranges.empty()) {
		MultiRange* newRange = ranges.insertMultiRange(
		    0); // Default option - allowed e.g. for a new Sound where the current process is the Ranges get set up
		        // before oscType is switched over to SAMPLE - but this can't happen for WAVETABLE so that's ok
		if (!newRange) {
			return nullptr;
		}

		newRange->topNote = 32767;
		return newRange;
	}

	else {
		return ranges.getElement(0);
	}
}

bool Source::hasAtLeastOneAudioFileLoaded() {
	for (int32_t e = 0; e < std::ssize(ranges); e++) {
		if (ranges.getElement(e)->getAudioFileHolder()->audioFile) {
			return true;
		}
	}
	return false;
}

void Source::doneReadingFromFile(Sound* sound) {

	SynthMode synthMode = sound->getSynthMode();

	if (synthMode == SynthMode::FM) {
		oscType = OscType::SINE;
	}
	else if (synthMode == SynthMode::RINGMOD) {
		oscType = std::min(oscType, kLastRingmoddableOscType);
	}

	bool isActualSampleOscillator = (synthMode != SynthMode::FM && oscType == OscType::SAMPLE);

	if (oscType == OscType::SAMPLE) {
		for (int32_t e = 0; e < std::ssize(ranges); e++) {
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

// Only to be called if already determined that oscType == OscType::SAMPLE
bool Source::hasAnyLoopEndPoint() {
	for (int32_t e = 0; e < std::ssize(ranges); e++) {
		MultisampleRange* range = (MultisampleRange*)ranges.getElement(e);
		if (range->sampleHolder.loopEndPos) {
			return true;
		}
	}
	return false;
}

// Return current osc type
OscType Source::getOscType() {
	return oscType;
}

// If setting to SAMPLE or WAVETABLE, you must call killAllVoices before this, because ranges is going to get
// emptied.
void Source::setOscType(OscType newType) {

	int32_t multiRangeSize;
	if (newType == OscType::SAMPLE) {
		multiRangeSize = sizeof(MultisampleRange);
possiblyDeleteRanges:
		if (ranges.currentElementSize() != multiRangeSize) {

doChangeType:
			Error error = ranges.changeType(multiRangeSize);
			if (error != Error::NONE) {
				ranges.clear();
				soundEditor.currentMultiRangeIndex = 0;
				goto doChangeType; // Can't fail now it's empty.
			}

			oscType = newType;

			getOrCreateFirstRange(); // Ensure there's at least 1. If this returns NULL and we're in the SoundEditor or
			                         // something, we're screwed.

			if (soundEditor.currentMultiRangeIndex >= 0 && soundEditor.currentMultiRangeIndex < std::ssize(ranges)) {
				soundEditor.currentMultiRange = ranges.getElement(soundEditor.currentMultiRangeIndex);
			}
		}
	}
	else if (newType == OscType::WAVETABLE) {
		multiRangeSize = sizeof(MultiWaveTableRange);
		goto possiblyDeleteRanges;
	}

	oscType = newType;
	if (docSource_ != nullptr) {
		docSource_->osc_type = static_cast<uint8_t>(newType);
	}
	if (oscType == OscType::DX7) {
		ensureDxPatch();
	}
	seedDocDxPatch(); // reflect the (possibly newly-created) patch into the record
}

DxPatch* Source::ensureDxPatch() {
	if (dxPatch == nullptr) {
		dxPatch = getDxEngine()->newPatch();
	}
	return dxPatch;
};

/*
    for (int32_t e = 0; e < std::ssize(ranges); e++) {
        ranges.getElement(e)->
    }

 */
