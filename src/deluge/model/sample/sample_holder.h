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

#pragma once

#include "definitions_cxx.hpp"
#include "model/sample/sample_playback_guide.h"
#include "storage/audio/audio_file_holder.h"
#include "util/d_string.h"

extern "C" {
#include "fatfs/ff.h"
}

class Sample;
class Cluster;

class SampleHolder : public AudioFileHolder {
public:
	SampleHolder();
	virtual ~SampleHolder();
	void unassignAllClusterReasons(bool beingDestructed = false);
	int64_t getEndPos(bool forTimeStretching = false);
	int64_t getDurationInSamples(bool forTimeStretching = false);
	void beenClonedFrom(SampleHolder* other, bool reversed);
	virtual void claimClusterReasons(bool reversed, int32_t clusterLoadInstruction = CLUSTER_ENQUEUE);
	int32_t getLengthInSamplesAtSystemSampleRate(bool forTimeStretching = false);
	int32_t getLoopLengthAtSystemSampleRate(bool forTimeStretching = false);
	void setAudioFile(AudioFile* newAudioFile, bool reversed = false, bool manuallySelected = false,
	                  int32_t clusterLoadInstruction = CLUSTER_ENQUEUE);

	// In samples.
	uint64_t startPos;
	uint64_t endPos; // Don't access this directly. Call getPos(). This variable may be beyond the end of the sample

	int32_t waveformViewScroll;
	int32_t waveformViewZoom; // 0 means neither of these vars set up yet

	int32_t neutralPhaseIncrement;

	Cluster* clustersForStart[kNumClustersLoadedAhead];

protected:
	void claimClusterReasonsForMarker(Cluster** clusters, uint32_t startPlaybackAtByte, int32_t playDirection,
	                                  int32_t clusterLoadInstruction);
	virtual void sampleBeenSet(bool reversed, bool manuallySelected) {}
};
