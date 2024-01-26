/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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
#include "model/sample/sample_low_level_reader.h"
#include <cstdint>

#define BUFFER_FILLING_OFF 0
#define BUFFER_FILLING_NEWER 1
#define BUFFER_FILLING_OLDER 2
#define BUFFER_FILLING_NEITHER 3

class VoiceSample;
class Voice;
class VoiceSamplePlaybackGuide;
class VoiceUnisonPartSource;
class Cluster;
class Sample;
class SampleCache;

class TimeStretcher {
public:
	TimeStretcher() = default;
	bool init(Sample* sample, VoiceSample* voiceSample, SamplePlaybackGuide* guide, int64_t newSamplePosBig,
	          int32_t numChannels, int32_t phaseIncrement, int32_t timeStretchRatio, int32_t playDirection,
	          int32_t priorityRating, int32_t fudgingNumSamplesTilLoop, LoopType loopingType);
	void reInit(int64_t newSamplePosBig, SamplePlaybackGuide* guide, VoiceSample* voiceSample, Sample* sample,
	            int32_t numChannels, int32_t timeStretchRatio, int32_t phaseIncrement, uint64_t combinedIncrement,
	            int32_t playDirection, LoopType loopingType, int32_t priorityRating);
	void beenUnassigned();
	void unassignAllReasonsForPercLookahead();
	void unassignAllReasonsForPercCacheClusters();
	bool hopEnd(SamplePlaybackGuide* guide, VoiceSample* voiceSample, Sample* sample, int32_t numChannels,
	            int32_t timeStretchRatio, int32_t phaseIncrement, uint64_t combinedIncrement, int32_t playDirection,
	            LoopType loopingType, int32_t priorityRating);

	void rememberPercCacheCluster(Cluster* cluster);
	void updateClustersForPercLookahead(Sample* sample, uint32_t sourceBytePos, int32_t playDirection);

	int32_t getSamplePos(int32_t playDirection);
	bool allocateBuffer(int32_t numChannels);

	void readFromBuffer(int32_t* oscBufferPos, int32_t numSamples, int32_t numChannels,
	                    int32_t numChannelsAfterCondensing, int32_t sourceAmplitudeNow, int32_t amplitudeIncrementNow,
	                    int32_t* bufferReadPos);

	void setupCrossfadeFromCache(SampleCache* cache, int32_t cacheBytePos, int32_t numChannels);

#if TIME_STRETCH_ENABLE_BUFFER
	void reassessWhetherToBeFillingBuffer(int32_t phaseIncrement, int32_t timeStretchRatio,
	                                      int32_t newBufferFillingMode, int32_t numChannels);
#endif
	TimeStretcher* nextUnassigned;

	int64_t samplePosBig; // In whole samples including both channels. From audioDataStart. <<'d by 24

	uint32_t crossfadeProgress; // Out of 16777216
	uint32_t crossfadeIncrement;

	int32_t samplesTilHopEnd;

	SampleLowLevelReader olderPartReader;

	int32_t* buffer;
	bool olderHeadReadingFromBuffer;
	bool hasLoopedBackIntoPreMargin;
	bool playHeadStillActive[2];
	uint8_t numTimesMissedHop;

	int32_t olderBufferReadPos; // In whole samples including both channels

#if TIME_STRETCH_ENABLE_BUFFER
	bool newerHeadReadingFromBuffer;
	int32_t newerBufferReadPos; // In whole samples including both channels

	uint8_t bufferFillingMode;
	int32_t bufferWritePos;        // In whole samples including both channels
	uint64_t bufferSamplesWritten; // Hopefully we can do away with the need for this
#endif

	Cluster* clustersForPercLookahead[kNumClustersLoadedAhead];

	Cluster* percCacheClustersNearby[2]; // Remembers and acts as a "reason" for the two most recently needed / accessed
	                                     // Clusters, basically

private:
	bool setupNewPlayHead(Sample* sample, VoiceSample* voiceSample, SamplePlaybackGuide* guide, int32_t newHeadBytePos,
	                      int32_t additionalOscPos, int32_t priorityRating, LoopType loopingType);
};

inline int32_t getTotalDifferenceAbs(int32_t* totals1, int32_t* totals2) {
	int32_t totalDifferenceAbs = 0;
	for (int32_t i = 0; i < TimeStretch::Crossfade::kNumMovingAverages; i++) {
		int32_t differenceAbsHere = totals2[i] - totals1[i];
		if (differenceAbsHere < 0)
			differenceAbsHere = -differenceAbsHere;
		totalDifferenceAbs += differenceAbsHere;
	}
	return totalDifferenceAbs;
}

inline int32_t getTotalChange(int32_t* totals1, int32_t* totals2) {
	int32_t totalChange = 0;
	for (int32_t i = 0; i < TimeStretch::Crossfade::kNumMovingAverages; i++) {
		totalChange += totals2[i];
	}

	for (int32_t i = 0; i < TimeStretch::Crossfade::kNumMovingAverages; i++) {
		totalChange -= totals1[i];
	}
	return totalChange;
}
