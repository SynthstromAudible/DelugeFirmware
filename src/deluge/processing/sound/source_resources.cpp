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

#include "processing/sound/source_resources.h"
#include "definitions_cxx.hpp"
#include "instruments/classic/sound_state.h"
#include "libdeluge/system.h" // AudioExcludedSection
#include "memory/memory_allocator_interface.h"
#include "model/sample/sample.h"
#include "processing/dx7/dx_engine.h"
#include "processing/engines/audio_engine.h"
#include "processing/ranges_retype.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multi_wave_table_range.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/wave_table/wave_table.h"
#include "util/functions.h"
#include <cstring>
#include <iterator>

SourceResources::SourceResources() = default;

SourceResources::~SourceResources() = default;

std::vector<deluge::synth::SourceRange> SourceResources::heldRanges() const {
	std::vector<deluge::synth::SourceRange> held;
	int32_t numRanges = std::ssize(ranges);
	held.reserve(numRanges);
	for (int32_t e = 0; e < numRanges; e++) {
		const auto* range = static_cast<const MultisampleRange*>(ranges.getElement(e));
		deluge::synth::SourceRange dr;
		dr.top_note = range->topNote;
		dr.file_path =
		    range->sampleHolder.audioFile ? range->sampleHolder.audioFile->filePath : range->sampleHolder.filePath;
		if (ranges.currentElementSize() == sizeof(MultisampleRange)) {
			dr.zone.start = range->sampleHolder.startPos;
			dr.zone.end = range->sampleHolder.endPos;
			dr.zone.loop_start = range->sampleHolder.loopStartPos;
			dr.zone.loop_end = range->sampleHolder.loopEndPos;
			dr.tuning.transpose = range->sampleHolder.transpose;
			dr.tuning.cents = range->sampleHolder.cents;
		}
		held.push_back(std::move(dr));
	}
	return held;
}

// Only for a sample oscillator (its sound's oscType is SAMPLE)
int32_t SourceResources::getLengthInSamplesAtSystemSampleRate(int32_t note, bool forTimeStretching) {
	MultiRange* range = getRange(note);
	if (range != nullptr) {
		return ((SampleHolder*)range->getAudioFileHolder())->getLengthInSamplesAtSystemSampleRate(forTimeStretching);
	}
	return 1; // Why did I put 1?
}

// This function has to give the same result as Sound::renderingVoicesInStereo(). The duplication is for optimization.
bool SourceResources::renderInStereo(OscType oscType, uint8_t unisonStereoSpread, uint8_t numUnison,
                                     SampleHolder* sampleHolder) {
	if (!AudioEngine::renderInStereo) {
		return false;
	}

	if (unisonStereoSpread && numUnison > 1) {
		return true;
	}

	return (oscType == OscType::SAMPLE && sampleHolder && sampleHolder->audioFile
	        && sampleHolder->audioFile->numChannels == 2)
	       || (oscType == OscType::INPUT_STEREO && (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn));
}

void SourceResources::detachAllAudioFiles() {
	for (int32_t e = 0; e < std::ssize(ranges); e++) {
		ranges.getElement(e)->getAudioFileHolder()->setAudioFile(nullptr);
	}
}

deluge::storage::Walk SourceResources::walkSampleDemands(deluge::storage::SampleLoadVisitor& visitor, bool reversed) {
	for (int32_t e = 0; e < std::ssize(ranges); e++) {
		if (visitor.rangeCheckpoint(e) == deluge::storage::Walk::Stop) {
			return deluge::storage::Walk::Stop;
		}
		visitor.demand({.holder = ranges.getElement(e)->getAudioFileHolder(),
		                .reversed = reversed,
		                .clusterLoadInstruction = CLUSTER_ENQUEUE,
		                .makeWaveTableWorkAtAllCosts = true});
	}

	return deluge::storage::Walk::Continue;
}

// Only for a sample oscillator (its sound's oscType is SAMPLE)
void SourceResources::followDirection(bool reversed) {
	for (int32_t e = 0; e < std::ssize(ranges); e++) {
		MultiRange* range = (MultisampleRange*)ranges.getElement(e);
		SampleHolder* holder = (SampleHolder*)range->getAudioFileHolder();
		Sample* sample = (Sample*)holder->audioFile;
		if (sample) {
			if (reversed && holder->endPos > sample->lengthInSamples) {
				holder->endPos = sample->lengthInSamples;
			}
			holder->claimClusterReasons(reversed);
		}
	}
}

MultiRange* SourceResources::getRange(int32_t note) {
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

int32_t SourceResources::getRangeIndex(int32_t note) {
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

MultiRange* SourceResources::getOrCreateFirstRange() {
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

bool SourceResources::hasAtLeastOneAudioFileLoaded() {
	for (int32_t e = 0; e < std::ssize(ranges); e++) {
		if (ranges.getElement(e)->getAudioFileHolder()->audioFile) {
			return true;
		}
	}
	return false;
}

// Only for a sample oscillator (its sound's oscType is SAMPLE)
bool SourceResources::hasAnyLoopEndPoint() {
	for (int32_t e = 0; e < std::ssize(ranges); e++) {
		MultisampleRange* range = (MultisampleRange*)ranges.getElement(e);
		if (range->sampleHolder.loopEndPos) {
			return true;
		}
	}
	return false;
}

// If setting to SAMPLE or WAVETABLE, you must call killAllVoices before this, because ranges is going to get
// emptied; likewise to or from an input type, whose live histories are replaced.
void SourceResources::setOscType(OscType newType) {

	int32_t multiRangeSize;
	if (newType == OscType::SAMPLE) {
		multiRangeSize = sizeof(MultisampleRange);
possiblyDeleteRanges:
		if (ranges.currentElementSize() != multiRangeSize) {
			bool cleared = false;
doChangeType:
			Error error = ranges.changeType(multiRangeSize);
			if (error != Error::NONE) {
				ranges.clear();
				cleared = true;
				goto doChangeType; // Can't fail now it's empty.
			}

			getOrCreateFirstRange(); // Ensure there's at least 1. If this returns NULL and we're in the SoundEditor or
			                         // something, we're screwed.
			RangesRetypeWatch::rangesRetyped(*this, cleared);
		}
	}
	else if (newType == OscType::WAVETABLE) {
		multiRangeSize = sizeof(MultiWaveTableRange);
		goto possiblyDeleteRanges;
	}

	reconcileLiveInputs(newType);
}

void SourceResources::reconcileLiveInputs(OscType type) {
	if (liveInputs.holds_for(type)) {
		return;
	}
	deluge::synth::LiveInputStore next = deluge::synth::LiveInputStore::for_type(type);
	{
		AudioExcludedSection section; // the render reads the histories each block
		liveInputs.swap(next);
	}
	// next holds the old histories, freed here, outside the section
}

/*
    for (int32_t e = 0; e < std::ssize(ranges); e++) {
        ranges.getElement(e)->
    }

 */
