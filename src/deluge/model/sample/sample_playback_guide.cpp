/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "model/sample/sample_playback_guide.h"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "model/sample/sample_holder.h"
#include "model/voice/voice_sample.h"
#include "playback/playback_handler.h"
#include "storage/audio/audio_file_manager.h"

SamplePlaybackGuide::SamplePlaybackGuide() {
	// TODO Auto-generated constructor stub
}

int32_t SamplePlaybackGuide::getFinalClusterIndex(Sample* sample, bool obeyMarkers, int32_t* getEndPlaybackAtByte) {

	GeneralMemoryAllocator::get().checkStack("SamplePlaybackGuide::getFinalClusterIndex");

	int32_t endPlaybackAtByteNow;
	// If cache, go right to the end of the waveform
	if (!obeyMarkers) {
		endPlaybackAtByteNow = (playDirection == 1)
		                           ? (sample->audioDataStartPosBytes + sample->audioDataLengthBytes)
		                           : (sample->audioDataStartPosBytes - sample->byteDepth * sample->numChannels);
		// Otherwise, use end or loop-end point
	}
	else {
		endPlaybackAtByteNow = getBytePosToEndOrLoopPlayback();
	}

	if (getEndPlaybackAtByte) {
		*getEndPlaybackAtByte = endPlaybackAtByteNow;
	}

	int32_t finalBytePos;

	if (playDirection == 1) {
		finalBytePos = endPlaybackAtByteNow - 1;
	}
	else {
		finalBytePos = endPlaybackAtByteNow + sample->byteDepth * sample->numChannels;
	}

	return finalBytePos >> Cluster::size_magnitude;
}

void SamplePlaybackGuide::setupPlaybackBounds(bool reversed) {
	playDirection = reversed ? -1 : 1;

	int32_t startPlaybackAtSample;
	int32_t endPlaybackAtSample;

	Sample* sample = (Sample*)audioFileHolder->audioFile;
	int32_t bytesPerSample = sample->numChannels * sample->byteDepth;

	// Forwards
	if (!reversed) {
		startPlaybackAtSample = ((SampleHolder*)audioFileHolder)->startPos;
		endPlaybackAtSample = ((SampleHolder*)audioFileHolder)->getEndPos();
	}

	// Reversed
	else {
		startPlaybackAtSample = ((SampleHolder*)audioFileHolder)->getEndPos() - 1;
		endPlaybackAtSample = ((SampleHolder*)audioFileHolder)->startPos - 1;
	}

	startPlaybackAtByte = sample->audioDataStartPosBytes + startPlaybackAtSample * bytesPerSample;
	endPlaybackAtByte = sample->audioDataStartPosBytes + endPlaybackAtSample * bytesPerSample;
}

// Returns how many samples in - after the start-sample
uint64_t SamplePlaybackGuide::getSyncedNumSamplesIn() {

	uint64_t lengthInSamples = ((SampleHolder*)audioFileHolder)->getDurationInSamples(true);

	uint32_t timeSinceLastInternalTick;

	uint32_t currentTickWithinSample =
	    playbackHandler.getCurrentInternalTickCount(&timeSinceLastInternalTick) - sequenceSyncStartedAtTick;

	uint32_t timePerInternalTick = playbackHandler.getTimePerInternalTick();

	if (timeSinceLastInternalTick >= timePerInternalTick) {
		timeSinceLastInternalTick = timePerInternalTick - 1; // Ensure it doesn't get bigger.
	}
	// If following external clock, that could happen
	// TODO: is that still necessary?
	uint64_t result =
	    (uint64_t)(((double)(lengthInSamples * currentTickWithinSample)
	                + (double)timeSinceLastInternalTick * (double)lengthInSamples / (double)timePerInternalTick
	                + (double)(sequenceSyncLengthTicks >> 1)) // Rounding
	               / (double)sequenceSyncLengthTicks);

	if (wrapSyncPosition && lengthInSamples > 0) {
		result = result % lengthInSamples;
	}

	return result;
}

int32_t SamplePlaybackGuide::getNumSamplesLaggingBehindSync(VoiceSample* voiceSample) {

	uint64_t idealNumSamplesIn = getSyncedNumSamplesIn();
	uint64_t idealSamplePos;
	if (playDirection == 1) {
		idealSamplePos = ((SampleHolder*)audioFileHolder)->startPos + idealNumSamplesIn;
	}
	else {
		idealSamplePos = ((SampleHolder*)audioFileHolder)->getEndPos(true) - 1 - idealNumSamplesIn;
	}

	uint64_t actualSamplePos = voiceSample->getPlaySample((Sample*)audioFileHolder->audioFile, this);

	return (int32_t)(idealSamplePos - actualSamplePos) * playDirection;
}

int32_t SamplePlaybackGuide::adjustPitchToCorrectDriftFromSync(VoiceSample* voiceSample, int32_t phaseIncrement) {

	// Not if not following external clock source, or clusters not set up yet (in the case of a very-late-start),
	// there's no need
	if (!playbackHandler.isExternalClockActive() || !voiceSample->clusters[0]) {
		return phaseIncrement;
	}

	int32_t numSamplesLaggingBehindSync = getNumSamplesLaggingBehindSync(voiceSample);

	int64_t newPhaseIncrement = phaseIncrement + ((int64_t)numSamplesLaggingBehindSync << 9);
	if (newPhaseIncrement < 1) {
		newPhaseIncrement = 1;
	}
	else if (newPhaseIncrement > 2147483647) {
		newPhaseIncrement = 2147483647;
	}
	return newPhaseIncrement;
}
