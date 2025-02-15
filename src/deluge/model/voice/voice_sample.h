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

#pragma once

#include "definitions_cxx.hpp"
#include "model/sample/sample_low_level_reader.h"

enum class LateStartAttemptStatus {
	SUCCESS = 0,
	FAILURE = 1,
	WAIT = 2,
};
class TimeStretcher;
class SampleCache;
class Sample;
class Voice;
class VoiceSamplePlaybackGuide;
class Source;
class SampleControls;

class VoiceSample final : public SampleLowLevelReader {
public:
	VoiceSample();

	void noteOn(SamplePlaybackGuide* guide, uint32_t samplesLate, int32_t priorityRating);
	bool noteOffWhenLoopEndPointExists(Voice* voice, VoiceSamplePlaybackGuide* voiceSource);

	void setupCacheLoopPoints(SamplePlaybackGuide* voiceSource, Sample* sample, LoopType loopingType);
	LateStartAttemptStatus attemptLateSampleStart(SamplePlaybackGuide* voiceSource, Sample* sample,
	                                              int64_t rawSamplesLate, int32_t numSamples = 0);
	void endTimeStretching();
	bool render(SamplePlaybackGuide* guide, int32_t* oscBuffer, int32_t numSamples, Sample* sample, int32_t numChannels,
	            LoopType loopingType, int32_t phaseIncrement, int32_t timeStretchRatio, int32_t amplitude,
	            int32_t amplitudeIncrement, int32_t bufferSize, InterpolationMode desiredInterpolationMode,
	            int32_t priorityRating);
	void beenUnassigned(bool wontBeUsedAgain);

	// AudioClips don't obey markers because they "fudge" instead.
	// Or if fudging can't happen cos no pre-margin, then
	// AudioClip::doTickForward() manually forces restart.
	bool shouldObeyMarkers() override { return (!cache && !timeStretcher && !forAudioClip); }

	void readSamplesResampledPossiblyCaching(int32_t** oscBufferPos, int32_t** oscBufferRPos, int32_t numSamples,
	                                         Sample* sample, int32_t jumpAmount, int32_t numChannels,
	                                         int32_t numChannelsAfterCondensing, int32_t phaseIncrement,
	                                         int32_t* sourceAmplitudeNow, int32_t amplitudeIncrement,
	                                         int32_t bufferSize, int32_t reduceMagnitudeBy = 1);

	bool sampleZoneChanged(SamplePlaybackGuide* voiceSource, Sample* sample, bool reversed, MarkerType markerType,
	                       LoopType loopingType, int32_t priorityRating, bool forAudioClip = false);
	int32_t getPlaySample(Sample* sample, SamplePlaybackGuide* guide);
	bool stopUsingCache(SamplePlaybackGuide* guide, Sample* sample, int32_t priorityRating, bool loopingAtLowLevel);
	bool possiblySetUpCache(SampleControls* sampleControls, SamplePlaybackGuide* guide, int32_t phaseIncrement,
	                        int32_t timeStretchRatio, int32_t priorityRating, LoopType loopingType);
	bool fudgeTimeStretchingToAvoidClick(Sample* sample, SamplePlaybackGuide* guide, int32_t phaseIncrement,
	                                     int32_t numSamplesTilLoop, int32_t playDirection, int32_t priorityRating);

	VoiceSample* nextUnassigned;

	uint32_t pendingSamplesLate; // This isn't used for AudioClips. And for samples in STRETCH mode, the exact number
	                             // isn't relevant - it gets recalculated
	TimeStretcher* timeStretcher;

	SampleCache* cache;
	int32_t cacheBytePos;
	bool doneFirstRenderYet;
	bool fudging;
	bool forAudioClip;              // This is a wee bit of a hack - but we need to be able to know this
	bool writingToCache;            // Value is only valid if cache assigned
	int32_t cacheLoopEndPointBytes; // 2147483647 means no looping. Will be set to sample end-point if looping there.
	                                // Gets re-set to 2147483647 when note "released"
	int32_t cacheEndPointBytes; // Will sometimes be the whole length of the sample. Wherever the red marker is. Or a
	                            // little further if it's the full length of the sample, to allow for timestretch /
	                            // interpolation ring-out
	uint32_t cacheLoopLengthBytes;

private:
	bool weShouldBeTimeStretchingNow(Sample* sample, SamplePlaybackGuide* guide, int32_t numSamples,
	                                 int32_t phaseIncrement, int32_t timeStretchRatio, int32_t playDirection,
	                                 int32_t priorityRating, LoopType loopingType);
	void switchToReadingCacheFromWriting();
	bool stopReadingFromCache();
};
