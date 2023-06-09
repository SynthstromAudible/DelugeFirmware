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

#ifndef SAMPLEREADER_H_
#define SAMPLEREADER_H_

#include "definitions.h"
#include "numericdriver.h"
#include "r_typedefs.h"

#define REASSESSMENT_ACTION_STOP_OR_LOOP 0
#define REASSESSMENT_ACTION_NEXT_CLUSTER 1

typedef __simd64_int16_t int16x4_t;

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

	void unassignAllReasons();
	void jumpForwardLinear(int numChannels, int byteDepth, uint32_t bitMask, int jumpAmount, int32_t phaseIncrement);
	void jumpForwardZeroes(int bufferSize, int numChannels, int32_t phaseIncrement);
	void interpolate(int32_t* sampleRead, int numChannels, int whichKernel);
	void interpolateLinear(int32_t* sampleRead, int numChannels, int whichKernel);
	void fillInterpolationBufferRetrospectively(Sample* sample, int bufferSize, int startI, int playDirection);
	void jumpBackSamples(Sample* sample, int numToJumpBack, int playDirection);
	void setupForPlayPosMovedIntoNewCluster(SamplePlaybackGuide* guide, Sample* sample, int bytePosWithinNewCluster,
	                                        int byteDepth);
	bool setupClusersForInitialPlay(SamplePlaybackGuide* guide, Sample* sample, int byteOvershoot = 0,
	                                bool justLooped = false, int priorityRating = 1);
	bool moveOnToNextCluster(SamplePlaybackGuide* guide, Sample* sample, int priorityRating = 1);
	bool changeClusterIfNecessary(SamplePlaybackGuide* guide, Sample* sample, bool loopingAtLowLevel,
	                              int priorityRating = 1);
	bool considerUpcomingWindow(SamplePlaybackGuide* guide, Sample* sample, int* numSamples, int32_t phaseIncrement,
	                            bool loopingAtLowLevel, int bufferSize, bool allowEndlessSilenceAtEnd = false,
	                            int priorityRating = 1);
	void setupReassessmentLocation(SamplePlaybackGuide* guide, Sample* sample);
	void misalignPlaybackParameters(Sample* sample);
	void realignPlaybackParameters(Sample* sample);
	bool reassessReassessmentLocation(SamplePlaybackGuide* guide, Sample* sample, int priorityRating);
	int32_t getPlayByteLowLevel(Sample* sample, SamplePlaybackGuide* guide,
	                            bool compensateForInterpolationBuffer = false);
	void cloneFrom(SampleLowLevelReader* other, bool stealReasons = false);
	bool setupClustersForPlayFromByte(SamplePlaybackGuide* guide, Sample* sample, int32_t startPlaybackAtByte,
	                                  int priorityRating);

	virtual bool shouldObeyMarkers() { return false; }

	void readSamplesNative(int32_t** __restrict__ oscBufferPos, int numSamplesTotal, Sample* sample, int jumpAmount,
	                       int numChannels, int numChannelsAfterCondensing, int32_t* amplitude,
	                       int32_t amplitudeIncrement, TimeStretcher* timeStretcher = NULL,
	                       bool bufferingToTimeStretcher = false);

	void readSamplesResampled(int32_t** __restrict__ oscBufferPos, int numSamples, Sample* sample, int jumpAmount,
	                          int numChannels, int numChannelsAfterCondensing, int32_t phaseIncrement,
	                          int32_t* amplitude, int32_t amplitudeIncrement, int bufferSize, bool writingCache,
	                          char** __restrict__ cacheWritePos, bool* doneAnySamplesYet, TimeStretcher* timeStretcher,
	                          bool bufferingToTimeStretcher, int whichKernel);

	bool readSamplesForTimeStretching(int32_t* oscBufferPos, SamplePlaybackGuide* guide, Sample* sample, int numSamples,
	                                  int numChannels, int numChannelsAfterCondensing, int32_t phaseIncrement,
	                                  int32_t amplitude, int32_t amplitudeIncrement, bool loopingAtLowLevel,
	                                  int jumpAmount, int bufferSize, TimeStretcher* timeStretcher,
	                                  bool bufferingToTimeStretcher, int whichPlayHead, int whichKernel,
	                                  int priorityRating);

	void bufferIndividualSampleForInterpolation(uint32_t bitMask, int numChannels, int byteDepth, char* playPosNow);
	void bufferZeroForInterpolation(int numChannels);

	uint32_t oscPos;
	char* currentPlayPos;
	char* reassessmentLocation;
	char* clusterStartLocation; // You're allowed to read from this location, but not move any further "back" past it
	uint8_t reassessmentAction;
	int8_t interpolationBufferSizeLastTime; // 0 if was previously switched off

	int16x4_t interpolationBuffer[2][INTERPOLATION_MAX_NUM_SAMPLES >> 2];

	Cluster* clusters[NUM_CLUSTERS_LOADED_AHEAD];

private:
	bool assignClusters(SamplePlaybackGuide* guide, Sample* sample, int clusterIndex, int priorityRating);
	bool fillInterpolationBufferForward(SamplePlaybackGuide* guide, Sample* sample, int interpolationBufferSize,
	                                    bool loopingAtLowLevel, int numSpacesToFill, int priorityRating);
};

#endif /* SAMPLEREADER_H_ */
