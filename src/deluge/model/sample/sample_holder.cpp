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

#include "model/sample/sample_holder.h"
#include "gui/ui/browser/sample_browser.h"
#include "hid/display/display.h"
#include "model/sample/sample.h"
#include "model/sample/sample_reader_bridge.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "util/functions.h"

static DelugeLoadMode loadModeFor(int32_t clusterLoadInstruction) {
	switch (clusterLoadInstruction) {
	case CLUSTER_LOAD_IMMEDIATELY:
		return DELUGE_LOAD_NOW;
	case CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE:
		return DELUGE_LOAD_NOW_OR_ENQUEUE;
	default:
		return DELUGE_LOAD_ENQUEUE; // CLUSTER_ENQUEUE
	}
}

SampleHolder::SampleHolder() {
	startPos = 0;
	endPos = 9999999;
	waveformViewZoom = 0;
	audioFileType = AudioFileType::SAMPLE;
}

SampleHolder::~SampleHolder() {

	// Don't call setSample() - that does writing to variables which isn't necessary

	if ((Sample*)audioFile) {
		unassignAllClusterReasons(true);
#if ALPHA_OR_BETA_VERSION
		if (!audioFile->isProjectReferenced()) {
			FREEZE_WITH_ERROR("E219"); // I put this here to try and catch an E004 Luc got
		}
#endif
		audioFile->removeReason("E396");
	}
}

void SampleHolder::beenClonedFrom(SampleHolder const* other, bool reversed) {
	filePath = other->filePath;
	if (other->audioFile) {
		setAudioFile(other->audioFile, reversed);
	}

	startPos = other->startPos;
	endPos = other->endPos;
	waveformViewScroll = other->waveformViewScroll;
	waveformViewZoom = other->waveformViewZoom;
}

void SampleHolder::unassignAllClusterReasons(bool beingDestructed) {
	if (clustersForStart_ != nullptr) {
		deluge_sample_reserve_close(clustersForStart_);
		if (!beingDestructed) {
			clustersForStart_ = nullptr;
		}
	}
}

int64_t SampleHolder::getEndPos(bool forTimeStretching) {
	if (forTimeStretching) {
		return endPos;
	}
	else {
		return std::min(endPos, ((Sample*)audioFile)->lengthInSamples);
	}
}

int64_t SampleHolder::getDurationInSamples(bool forTimeStretching) {
	return getEndPos(forTimeStretching) - startPos;
}

int32_t SampleHolder::getLengthInSamplesAtSystemSampleRate(bool forTimeStretching) {
	uint64_t lengthInSamples = getDurationInSamples(forTimeStretching);
	if (neutralPhaseIncrement == kMaxSampleValue) {
		return lengthInSamples;
	}
	else {
		return (lengthInSamples << 24) / neutralPhaseIncrement;
	}
}

// returns loop length in ticks from the sample waveform start and end positions selected
int32_t SampleHolder::getLoopLengthAtSystemSampleRate(bool forTimeStretching) {
	if (audioFile) {
		double loopLength = (double)getLengthInSamplesAtSystemSampleRate(forTimeStretching)
		                    / playbackHandler.getTimePerInternalTickFloat();

		return static_cast<int32_t>(loopLength);
	}
	return getCurrentClip()->loopLength();
}

void SampleHolder::setAudioFile(AudioFile* newSample, bool reversed, bool manuallySelected,
                                int32_t clusterLoadInstruction) {

	AudioFileHolder::setAudioFile(newSample, reversed, manuallySelected, clusterLoadInstruction);

	if (audioFile) {

		if (manuallySelected && ((Sample*)audioFile)->tempFilePathForRecording.empty()) {
			sampleBrowser.lastFilePathLoaded = filePath;
		}

		uint32_t lengthInSamples = ((Sample*)audioFile)->lengthInSamples;

		// If we're here as a result of the user having manually selected a new file, set the zone to its actual length.
		if (manuallySelected) {
			startPos = 0;
			endPos = lengthInSamples;
		}

		// Otherwise, simply make sure that the zone doesn't exceed the length of the sample
		else {
			startPos = std::min<uint64_t>(startPos, lengthInSamples);
			if (endPos == 0 || endPos == 9999999) {
				endPos = lengthInSamples;
			}
			if (endPos <= startPos) {
				startPos = 0;
			}
		}

		sampleBeenSet(reversed, manuallySelected);

		if (!audioFile) {
			FREEZE_WITH_ERROR("i031"); // Trying to narrow down E368 that Kevin F got
		}

		claimClusterReasons(reversed, clusterLoadInstruction);
	}
}

constexpr int32_t kMarkerSamplesBeforeToClaim = 150;

// Reassesses which Clusters we want to be a "reason" for.
// Ensure there is a sample before you call this.
void SampleHolder::claimClusterReasons(bool reversed, int32_t clusterLoadInstruction) {

	if (!audioFile) {
		FREEZE_WITH_ERROR("E368");
		return; // audioFile is dereferenced below
	}

	// unassignAllReasons(); // This now happens as part of reassessPosForMarker(), called below

	int32_t playDirection = reversed ? -1 : 1;
	int32_t bytesPerSample = audioFile->numChannels * ((Sample*)audioFile)->byteDepth;

	// This code basically copied from VoiceSource::setupPlaybackBounds()
	int32_t startPlaybackAtSample;

	if (!reversed) {
		startPlaybackAtSample = (int64_t)startPos - kMarkerSamplesBeforeToClaim;
		if (startPlaybackAtSample < 0) {
			startPlaybackAtSample = 0;
		}
	}
	else {
		startPlaybackAtSample = getEndPos() - 1 + kMarkerSamplesBeforeToClaim;
		if (startPlaybackAtSample > ((Sample*)audioFile)->lengthInSamples - 1) {
			startPlaybackAtSample = ((Sample*)audioFile)->lengthInSamples - 1;
		}
	}

	startPlaybackAtByte_ = ((Sample*)audioFile)->audioDataStartPosBytes + startPlaybackAtSample * bytesPerSample;

	claimClusterReasonsForMarker(clustersForStart_, startPlaybackAtByte_, playDirection, clusterLoadInstruction);
}

void SampleHolder::claimClusterReasonsForMarker(DelugeSampleReservation*& reservation, uint32_t startPlaybackAtByte,
                                                int32_t playDirection, int32_t clusterLoadInstruction) {

	uint32_t sourceId = deluge::sample::source_id_for(*(Sample*)audioFile);
	int32_t bytesPerSample = audioFile->numChannels * ((Sample*)audioFile)->byteDepth;
	uint64_t markerFrame = (startPlaybackAtByte - ((Sample*)audioFile)->audioDataStartPosBytes) / bytesPerSample;
	DelugeLoadMode mode = loadModeFor(clusterLoadInstruction);
	if (reservation == nullptr) {
		reservation = deluge_sample_reserve_open(sourceId, markerFrame, playDirection, mode);
	}
	else {
		deluge_sample_reserve_move(reservation, sourceId, markerFrame, playDirection, mode);
	}
}
