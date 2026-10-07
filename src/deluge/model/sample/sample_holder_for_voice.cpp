/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "model/sample/sample_holder_for_voice.h"
#include "io/debug/log.h"
#include "model/sample/sample.h"
#include "model/sample/sample_transpose.h"
#include "processing/source.h"
#include "storage/cluster/cluster.h"
#include "storage/storage_manager.h"
#include <cmath>

SampleHolderForVoice::SampleHolderForVoice() {
	loopStartPos = 0;
	loopEndPos = 0;

	transpose = 0;
	cents = 0;

	loopLocked = false;
}

SampleHolderForVoice::~SampleHolderForVoice() {
	// We have to unassign reasons here, even though our parent destructor will call unassignAllReasons() - our
	// overriding of that virtual function won't happen as we've already been destructed!
	if (clustersForLoopStart_ != nullptr) {
		deluge_sample_reserve_close(clustersForLoopStart_);
	}
}

void SampleHolderForVoice::unassignAllClusterReasons(bool beingDestructed) {
	SampleHolder::unassignAllClusterReasons(beingDestructed);
	if (clustersForLoopStart_ != nullptr) {
		deluge_sample_reserve_close(clustersForLoopStart_);
		if (!beingDestructed) {
			clustersForLoopStart_ = nullptr;
		}
	}
}

// Reassesses which Clusters we want to be a "reason" for.
// Ensure there is a sample before you call this.
void SampleHolderForVoice::claimClusterReasons(bool reversed, int32_t clusterLoadInstruction) {

#if ALPHA_OR_BETA_VERSION
	if (!audioFile) {
		FREEZE_WITH_ERROR("i030"); // Trying to narrow down E368 that Kevin F got
	}
#endif

	SampleHolder::claimClusterReasons(reversed, clusterLoadInstruction);

	int32_t playDirection = reversed ? -1 : 1;
	int32_t bytesPerSample = ((Sample*)audioFile)->numChannels * ((Sample*)audioFile)->byteDepth;

	int32_t loopStartPlaybackAtSample = reversed ? loopEndPos : loopStartPos;

	if (reversed) { // Don't mix this with the above - we want to keep 0s as 0
		if (loopStartPlaybackAtSample) {
			loopStartPlaybackAtSample--;
		}
	}

	Sample* sample = (Sample*)audioFile;
	uint64_t durationClusters =
	    static_cast<uint64_t>(getDurationInSamples()) * bytesPerSample >> Cluster::size_magnitude;
	int64_t nextClusterStartByte = static_cast<int64_t>(startPlaybackAtByte_) + playDirection * 2 * Cluster::size;
	bool nextClustersInSample =
	    sample->isLengthKnown() && nextClusterStartByte >= static_cast<int64_t>(sample->audioDataStartPosBytes)
	    && static_cast<uint64_t>(nextClusterStartByte) < sample->audioDataStartPosBytes + sample->audioDataLengthBytes;

	// A short sample can stay entirely cached: pin the clusters following the start marker's window.
	if (durationClusters <= kMaxNumClustersLoadedAhead + kNumClustersLoadedAhead && nextClustersInSample) {
		claimClusterReasonsForMarker(clustersForLoopStart_, nextClusterStartByte, playDirection,
		                             clusterLoadInstruction);
	}

	else if (loopStartPlaybackAtSample) {
		int32_t loopStartPlaybackAtByte = sample->audioDataStartPosBytes + loopStartPlaybackAtSample * bytesPerSample;
		claimClusterReasonsForMarker(clustersForLoopStart_, loopStartPlaybackAtByte, playDirection,
		                             clusterLoadInstruction);
	}

	// Or if no loop start point now, clear out any reasons we had before
	else {
		if (clustersForLoopStart_ != nullptr) {
			deluge_sample_reserve_close(clustersForLoopStart_);
			clustersForLoopStart_ = nullptr;
		}
	}
}

void SampleHolderForVoice::setCents(int32_t newCents) {
	cents = newCents;
	recalculateFineTuner();
}

void SampleHolderForVoice::recalculateFineTuner() {
	fineTuner.setup((int32_t)cents * 42949672);
}

uint32_t SampleHolderForVoice::getMSecLimit(Source* source) {
	if (source->repeatMode == SampleRepeatMode::STRETCH) {
		return 9999999;
	}
	else {
		if (!audioFile) {
			return 0;
		}
		else {
			return ((Sample*)audioFile)->getLengthInMSec();
		}
	}
}

void SampleHolderForVoice::setTransposeAccordingToSamplePitch(bool minimizeOctaves, bool rangeCoversJustOneNote,
                                                              int32_t thatOneNote) {
	// The note is the detection's: every caller has run it. Nothing is scanned here.
	const float midiNote = ((Sample*)audioFile)->midiNote;
	if (midiNote != MIDI_NOTE_ERROR && midiNote != MIDI_NOTE_UNSET) {
		const deluge::sample::SampleTranspose t =
		    deluge::sample::transposeForNote(midiNote, minimizeOctaves, rangeCoversJustOneNote, thatOneNote);
		transpose = t.semitones;
		setCents(t.cents);
	}
}

void SampleHolderForVoice::sampleBeenSet(bool reversed, bool manuallySelected) {

	neutralPhaseIncrement = ((uint64_t)((Sample*)audioFile)->sampleRate << 24) / kSampleRate;

	uint32_t lengthInSamples = ((Sample*)audioFile)->lengthInSamples;

	// If we're here as a result of the user having manually selected a new file, set up loop points
	if (manuallySelected) {
		loopStartPos = 0;
		loopEndPos = 0;

		if (((Sample*)audioFile)->fileLoopEndSamples && ((Sample*)audioFile)->fileLoopEndSamples <= lengthInSamples) {

			int32_t loopLength = ((Sample*)audioFile)->fileLoopEndSamples - ((Sample*)audioFile)->fileLoopStartSamples;

			int32_t lengthAfterLoop = lengthInSamples - ((Sample*)audioFile)->fileLoopEndSamples;

			if (loopLength >= lengthAfterLoop) {
				endPos = ((Sample*)audioFile)->fileLoopEndSamples;
			}
			else {
				loopEndPos = ((Sample*)audioFile)->fileLoopEndSamples;
			}

			// Grab loop start from file too, if it's not erroneously late
			if (((Sample*)audioFile)->fileLoopStartSamples < lengthInSamples
			    && (!((Sample*)audioFile)->fileLoopEndSamples
			        || ((Sample*)audioFile)->fileLoopStartSamples < ((Sample*)audioFile)->fileLoopEndSamples)) {
				loopStartPos =
				    ((Sample*)audioFile)->fileLoopStartSamples; // If it's 0, that'll translate to meaning no loop start
				                                                // pos, which is exactly what we want in that case
			}
		}
	}

	// Or if not manually selected
	else {

		// Check for illegal values - we could have a problem if an audio file got replaced with a shorter one
		if (loopEndPos > ((Sample*)audioFile)->lengthInSamples) {
			loopEndPos = 0;
		}
		if (loopStartPos > ((Sample*)audioFile)->lengthInSamples) {
			loopStartPos = 0;
		}

		if (loopEndPos && loopStartPos >= loopEndPos) {
			loopStartPos = 0; // It's arbitrary which one we set to 0
		}
	}
}
