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

#pragma once

#include "arm_neon_shim.h"

#include "definitions_cxx.hpp"
#include <array>
#include <cstdint>
#define REASSESSMENT_ACTION_STOP_OR_LOOP 0
#define REASSESSMENT_ACTION_NEXT_CLUSTER 1

class VoiceSamplePlaybackGuide;
class Voice;
class Sample;
class Cluster;
class TimeStretcher;
class SamplePlaybackGuide;

class SampleLowLevelReader {
public:
	SampleLowLevelReader();
	~SampleLowLevelReader();

	void unassignAllReasons(bool wontBeUsedAgain);
	void jumpForwardLinear(int32_t numChannels, int32_t byteDepth, uint32_t bitMask, int32_t jumpAmount,
	                       int32_t phaseIncrement);
	void jumpForwardZeroes(int32_t bufferSize, int32_t numChannels, int32_t phaseIncrement);
	void fillInterpolationBufferRetrospectively(Sample* sample, int32_t bufferSize, int32_t startI,
	                                            int32_t playDirection);
	void jumpBackSamples(Sample* sample, int32_t numToJumpBack, int32_t playDirection);
	void setupForPlayPosMovedIntoNewCluster(SamplePlaybackGuide* guide, Sample* sample, int32_t bytePosWithinNewCluster,
	                                        int32_t byteDepth);
	bool setupClusersForInitialPlay(SamplePlaybackGuide* guide, Sample* sample, int32_t byteOvershoot = 0,
	                                bool justLooped = false, int32_t priorityRating = 1);
	bool moveOnToNextCluster(SamplePlaybackGuide* guide, Sample* sample, int32_t priorityRating = 1);
	bool changeClusterIfNecessary(SamplePlaybackGuide* guide, Sample* sample, bool loopingAtLowLevel,
	                              int32_t priorityRating = 1);
	bool considerUpcomingWindow(SamplePlaybackGuide* guide, Sample* sample, int32_t* numSamples, int32_t phaseIncrement,
	                            bool loopingAtLowLevel, int32_t bufferSize, bool allowEndlessSilenceAtEnd = false,
	                            int32_t priorityRating = 1);
	void setupReassessmentLocation(SamplePlaybackGuide* guide, Sample* sample);
	void misalignPlaybackParameters(Sample* sample);
	void realignPlaybackParameters(Sample* sample);
	bool reassessReassessmentLocation(SamplePlaybackGuide* guide, Sample* sample, int32_t priorityRating);
	int32_t getPlayByteLowLevel(Sample* sample, SamplePlaybackGuide* guide,
	                            bool compensateForInterpolationBuffer = false);
	void cloneFrom(SampleLowLevelReader* other, bool stealReasons = false);
	bool setupClustersForPlayFromByte(SamplePlaybackGuide* guide, Sample* sample, int32_t startPlaybackAtByte,
	                                  int32_t priorityRating);

	virtual bool shouldObeyMarkers() { return false; }

	void readSamplesNative(int32_t** __restrict__ oscBufferPos, int32_t numSamplesTotal, Sample* sample,
	                       int32_t jumpAmount, int32_t numChannels, int32_t numChannelsAfterCondensing,
	                       int32_t* amplitude, int32_t amplitudeIncrement, TimeStretcher* timeStretcher = NULL,
	                       bool bufferingToTimeStretcher = false);

	void readSamplesResampled(int32_t** __restrict__ oscBufferPos, int32_t numSamples, Sample* sample,
	                          int32_t jumpAmount, int32_t numChannels, int32_t numChannelsAfterCondensing,
	                          int32_t phaseIncrement, int32_t* amplitude, int32_t amplitudeIncrement,
	                          int32_t bufferSize, bool writingCache, char** __restrict__ cacheWritePos,
	                          bool* doneAnySamplesYet, TimeStretcher* timeStretcher, bool bufferingToTimeStretcher,
	                          int32_t whichKernel);

	bool readSamplesForTimeStretching(int32_t* oscBufferPos, SamplePlaybackGuide* guide, Sample* sample,
	                                  int32_t numSamples, int32_t numChannels, int32_t numChannelsAfterCondensing,
	                                  int32_t phaseIncrement, int32_t amplitude, int32_t amplitudeIncrement,
	                                  bool loopingAtLowLevel, int32_t jumpAmount, int32_t bufferSize,
	                                  TimeStretcher* timeStretcher, bool bufferingToTimeStretcher,
	                                  int32_t whichPlayHead, int32_t whichKernel, int32_t priorityRating);

	void bufferIndividualSampleForInterpolation(uint32_t bitMask, int32_t numChannels, int32_t byteDepth,
	                                            char* playPosNow);
	void bufferZeroForInterpolation(int32_t numChannels);

	uint32_t oscPos;
	char* currentPlayPos;
	char* reassessmentLocation;
	char* clusterStartLocation; // You're allowed to read from this location, but not move any further "back" past it
	uint8_t reassessmentAction;
	int8_t interpolationBufferSizeLastTime; // 0 if was previously switched off

	std::array<std::array<int16x4_t, kInterpolationMaxNumSamples / 4>, 2> interpolationBuffer;

	Cluster* clusters[kNumClustersLoadedAhead];

private:
	bool assignClusters(SamplePlaybackGuide* guide, Sample* sample, int32_t clusterIndex, int32_t priorityRating);
	bool fillInterpolationBufferForward(SamplePlaybackGuide* guide, Sample* sample, int32_t interpolationBufferSize,
	                                    bool loopingAtLowLevel, int32_t numSpacesToFill, int32_t priorityRating);
};
