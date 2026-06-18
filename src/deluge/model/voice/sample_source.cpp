/*
 * Copyright © 2014-2023 Synthstrom Audible Limited, 2026 Synthstrom Audible Limited
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

#include "model/voice/sample_source.h"
#include "definitions_cxx.hpp"
#include "model/sample/sample.h"
#include "model/sample/sample_cache.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/song/song.h"
#include "model/voice/voice.h"
#include "model/voice/voice_sample.h"
#include "model/voice/voice_sample_playback_guide.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/multi_range/multisample_range.h"
#include "util/fixedpoint.h"
#include "util/lookuptables/lookuptables.h"
#include "util/misc.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>

using namespace deluge;
namespace params = deluge::modulation::params;

bool SampleSource::noteOn(::Source* source, VoiceSamplePlaybackGuide* guide, uint32_t samplesLate,
                          int32_t priorityRating) {

	// Cache the per-note source config + guide (stable for the note); render reads these instead of reaching Voice&.
	source_ = source;
	guide_ = guide;

	if ((guide->audioFileHolder == nullptr) || (guide->audioFileHolder->audioFile == nullptr)
	    || ((Sample*)guide->audioFileHolder->audioFile)->unplayable) {
		return true; // We didn't succeed, but don't want to stop the whole Voice from sounding necessarily
	}

	if (voiceSample_ == nullptr) { // We might actually already have one, and just be restarting this voice
		voiceSample_ = AudioEngine::solicitVoiceSample();
		if (voiceSample_ == nullptr) [[unlikely]] {
			return false;
		}
	}
	else {
		// if we're restarting a voice we need to clear it's reasons,
		// otherwise we'll increase now but only reduce by one at note off
		// not quite thread safe - if the sample is shorter than 64k
		// an allocation before setupClustersForInitialPlay could steal it
		voiceSample_->beenUnassigned(false);
	}
	voiceSample_->noteOn(guide, samplesLate, priorityRating);
	if (samplesLate != 0u) {
		return true; // We're finished in this case
	}
	return voiceSample_->setupClusersForInitialPlay(guide, (Sample*)guide->audioFileHolder->audioFile, 0, false, 1);
}

void SampleSource::unassign(bool deletingSong) {
	if (voiceSample_ != nullptr) {
		voiceSample_->beenUnassigned(deletingSong);
		AudioEngine::voiceSampleUnassigned(voiceSample_);
		voiceSample_ = nullptr;
	}
	source_ = nullptr;
	guide_ = nullptr;
}

bool SampleSource::cloneVoiceSampleFrom(const SampleSource& from) {
	voiceSample_ = AudioEngine::solicitVoiceSample();
	if (voiceSample_ == nullptr) {
		return false;
	}

	VoiceSample& newVoiceSample = *voiceSample_;
	VoiceSample& oldVoiceSample = *from.voiceSample_;

	// Just clones the SampleLowLevelReader stuff
	newVoiceSample = SampleLowLevelReader(oldVoiceSample);
	newVoiceSample.pendingSamplesLate = oldVoiceSample.pendingSamplesLate;
	newVoiceSample.doneFirstRenderYet = true;
	return true;
}

bool SampleSource::noteOffAtLoopEnd(Voice* voice, VoiceSamplePlaybackGuide* guide) {
	return voiceSample_->noteOffWhenLoopEndPointExists(voice, guide);
}

bool SampleSource::zoneChanged(VoiceSamplePlaybackGuide* guide, Sample* sample, bool reversed, MarkerType markerType,
                               LoopType loopingType, int32_t priorityRating) {
	return voiceSample_->sampleZoneChanged(guide, sample, reversed, markerType, loopingType, priorityRating);
}

int32_t SampleSource::getPlayByteLowLevel(Sample* sample, VoiceSamplePlaybackGuide* guide) {
	return voiceSample_->getPlayByteLowLevel(sample, guide);
}

deluge::synth::SourceRenderResult SampleSource::renderSource(const deluge::synth::SourceRenderContext& ctx) {
	int32_t* __restrict__ oscBuffer = ctx.oscBuffer;
	const int32_t numSamples = ctx.numSamples;
	const bool stereoBuffer = ctx.stereoBuffer;
	const int32_t sourceAmplitude = ctx.sourceAmplitude;
	const int32_t amplitudeIncrement = ctx.amplitudeIncrement;
	uint32_t phaseIncrement = ctx.phaseIncrement; // mutated below by getPitchAndSpeedParams (taken by address)
	const uint32_t phaseIncrementStoredValue = ctx.phaseIncrementStoredValue;
	const bool stereoUnison = ctx.stereoUnison;
	const int32_t amplitudeL = ctx.amplitudeL;
	const int32_t amplitudeR = ctx.amplitudeR;
	int32_t* stereoUnisonScratch = ctx.stereoScratch;

	Sample* sample = (Sample*)guide_->audioFileHolder->audioFile;
	VoiceSample* voiceSample = voiceSample_;

	int32_t numChannels = (sample->numChannels == 2) ? 2 : 1;

	// (The legacy TEST_SAMPLE_LOOP_POINTS fuzzing block was removed here: it mutated voice/sound state via
	// Voice&/paramManager, incompatible with the synth::Source contract. A debug-only #ifdef, never in normal or
	// golden builds; reintroduce app-side if wanted.)

	// First figure out the time-stretching amount

	int32_t pitchAdjustNeutralValue = ((SampleHolder*)guide_->audioFileHolder)->neutralPhaseIncrement;
	uint32_t timeStretchRatio;
	uint32_t noteLengthInSamples;

	bool stillOk = getPitchAndSpeedParams(source_, guide_, &phaseIncrement, &timeStretchRatio, &noteLengthInSamples);
	if (!stillOk) {
		return deluge::synth::SourceRenderResult::UNASSIGN;
	}

	// If user unmuted mid-note...
	bool tryToStartMidNote = voiceSample->pendingSamplesLate;
	if (tryToStartMidNote) [[unlikely]] {

		int32_t rawSamplesLate;

		// Synced / STRETCH - it's super easy.
		if (source_->repeatMode == SampleRepeatMode::STRETCH) {
			rawSamplesLate = guide_->getSyncedNumSamplesIn();
		}

		// Or, normal - it needs a bit more explanation.
		else {
			// We have to ignore any pitch modulation, aka the "patched" value for phaseIncrement: we have to
			// use phaseIncrementStoredValue instead, which is the pre-modulation/patching value. But we also
			// have to forget about the timeStretchRatio we calculated above with the call to
			// getPitchAndSpeedParams(), and instead calculate a special version of this with this call to
			// getSpeedParamForNoSyncing(), which is what getPitchAndSpeedParams() itself calls when not in
			// STRETCH mode, which we've already determined we're not in, and crucially pass it the not-patched
			// phaseIncrementStoredValue. This fix was done in September 2020 after bug
			// report from Clyde.
			uint32_t timeStretchRatioWithoutModulation = getSpeedParamForNoSyncing(
			    source_, phaseIncrementStoredValue, ((SampleHolder*)guide_->audioFileHolder)->neutralPhaseIncrement);

			// Cool, so now we've got phaseIncrement and timeStretchRatio equivalent values which will indicate
			// our correct play position into the sample regardless of pitch modulation (almost always vibrato).
			rawSamplesLate = ((((uint64_t)voiceSample->pendingSamplesLate * phaseIncrementStoredValue) >> 24)
			                  * timeStretchRatioWithoutModulation)
			                 >> 24;
		}

		LateStartAttemptStatus result = voiceSample->attemptLateSampleStart(guide_, sample, rawSamplesLate, numSamples);

		if (result == LateStartAttemptStatus::FAILURE) {
			return deluge::synth::SourceRenderResult::UNASSIGN;
		}
		else if (result == LateStartAttemptStatus::WAIT) {
			return deluge::synth::SourceRenderResult::SKIP;
		}
		// Otherwise, it started fine!
	}

	LoopType loopingType = guide_->getLoopingType(*source_);

	int32_t interpolationBufferSize;

	// If pitch adjustment...
	if (phaseIncrement != kMaxSampleValue) {

		// Work out what quality we're going to do that at
		interpolationBufferSize = source_->sampleControls.getInterpolationBufferSize(phaseIncrement);

		// And if first render, and other conditions met, see if we can use cache.
		// It may seem like it'd be a good idea to try and set this up on note-on, rather than here in the
		// rendering routine, but I tried that and the fact is that it means a bunch of extra computation has to
		// happen to work out pitch and timestretch there as well as here (where it'll be worked out anyway),
		// including checking the result of patching / modulation (and we *do* allow caching where, say,
		// velocity or note is affecting pitch), and stretch-syncing.
		if (!voiceSample->doneFirstRenderYet && !tryToStartMidNote && ctx.portaEnvelopePos == 0xFFFFFFFF) { // No porta

			// If looping, make sure the loop isn't too short. If so, caching just wouldn't sound good /
			// accurate
			if (loopingType != LoopType::NONE) {
				SampleHolderForVoice* holder = (SampleHolderForVoice*)guide_->audioFileHolder;
				int32_t loopStart = holder->loopStartPos ? holder->loopStartPos : holder->startPos;
				int32_t loopEnd = holder->loopEndPos ? holder->loopEndPos : holder->endPos;

				int32_t loopLength = loopEnd - loopStart;
				loopLength = std::abs(loopLength);
				uint64_t phaseIncrementTimesTimeStretchRatio =
				    ((uint64_t)(uint32_t)phaseIncrement * (uint32_t)timeStretchRatio) >> 24;
				int32_t loopLengthCached = ((uint64_t)(uint32_t)loopLength << 24) / phaseIncrementTimesTimeStretchRatio;
				if (loopLengthCached < 2205) {
					goto dontUseCache; // Limit is 50mS i.e. 20hZ
				}
			}

			// If a changeable source is patched to pitch, don't cache. This scan was hoisted app-side into
			// ctx.pitchModDisallowsCache (it reaches the patch cables + the aftertouch/sidechain source values),
			// so render needn't reach paramManager / the param planes.
			if (ctx.pitchModDisallowsCache) {
				goto dontUseCache;
			}

			{
				// If still here, we can use cache
				bool everythingOk = voiceSample->possiblySetUpCache(&source_->sampleControls, guide_, phaseIncrement,
				                                                    timeStretchRatio, ctx.priorityRating, loopingType);
				if (!everythingOk) {
					return deluge::synth::SourceRenderResult::UNASSIGN;
				}
			}

dontUseCache: {}
		}
	}

	int32_t* renderBuffer = oscBuffer;

	if (stereoUnison) [[unlikely]] {
		// TODO: I first wanted to integrate this with voiceSample->render()'s own
		// amplitude control but it is just too complex - multiple copies of
		// the amp logic depending if caching is used or not, timestretching or not,
		// for now settle for "don't pay what you don't use", i e no extra copies/maths
		// if you don't use unison stereo on a sample - bfredl
		renderBuffer = stereoUnisonScratch; // note: spareRenderingBuffer[2] and [3] are used
		memset(renderBuffer, 0, 2 * SSI_TX_BUFFER_NUM_SAMPLES * sizeof(int32_t));
	}

	// We no longer do caching when there's just time stretching with no pitch adjustment, because the time
	// stretching algorithm is so efficient, playing back the cache is hardly any faster than just doing the
	// time stretching (once perc info has been cached) - and, crucially, creating / writing to the cache in the
	// first place is quite inefficient when time stretching, because when we're not writing to the cache, that
	// allows us to do a special optimization not otherwise available (that is, combining the amplitude
	// increments for the hop crossfades with the overall voice ones, and having multiple crossfading hops write
	// directly to the osc buffer).

	bool stillActive =
	    voiceSample->render(guide_, renderBuffer, numSamples, sample, numChannels, loopingType, phaseIncrement,
	                        timeStretchRatio, sourceAmplitude, amplitudeIncrement, interpolationBufferSize,
	                        source_->sampleControls.interpolationMode, ctx.priorityRating);

	if (stereoUnison) {
		if (numChannels == 2) {
			// TODO: society if renderBasicSource() took a StereoSample[] buffer already
			for (int32_t i = 0; i < numSamples; i++) {
				oscBuffer[(i << 1)] += multiply_32x32_rshift32(renderBuffer[(i << 1)], amplitudeL) << 2;
				oscBuffer[(i << 1) + 1] += multiply_32x32_rshift32(renderBuffer[(i << 1) + 1], amplitudeR) << 2;
			}
		}
		else {
			// TODO: if render buffer was typed we could use addPannedMono()
			for (int32_t i = 0; i < numSamples; i++) {
				oscBuffer[(i << 1)] += multiply_32x32_rshift32(renderBuffer[i], amplitudeL) << 2;
				oscBuffer[(i << 1) + 1] += multiply_32x32_rshift32(renderBuffer[i], amplitudeR) << 2;
			}
		}
	}

	if (!stillActive) {
		return deluge::synth::SourceRenderResult::UNASSIGN;
	}
	return deluge::synth::SourceRenderResult::ACTIVE;
}

bool SampleSource::getPitchAndSpeedParams(::Source* source, VoiceSamplePlaybackGuide* guide, uint32_t* phaseIncrement,
                                          uint32_t* timeStretchRatio, uint32_t* noteLengthInSamples) {

	int32_t pitchAdjustNeutralValue = ((SampleHolder*)guide->audioFileHolder)->neutralPhaseIncrement;

	// If syncing...
	if (guide->sequenceSyncLengthTicks) {

		*timeStretchRatio = kMaxSampleValue;

		// That is, after converted to 44.1kHz
		uint32_t sampleLengthInSamples =
		    ((SampleHolder*)guide->audioFileHolder)->getLengthInSamplesAtSystemSampleRate(true);
		*noteLengthInSamples = (playbackHandler.getTimePerInternalTickBig() * guide->sequenceSyncLengthTicks)
		                       >> 32; // No rounding. Should be fine?

		// To stop things getting insane, limit to 32x speed
		if ((sampleLengthInSamples >> 5) > *noteLengthInSamples) {
			return false;
		}

		// If time stretch, achieve syncing that way
		if (source->sampleControls.pitchAndSpeedAreIndependent) {
			*timeStretchRatio = (uint64_t)kMaxSampleValue * (uint64_t)sampleLengthInSamples / *noteLengthInSamples;

			// And if pitch was manually adjusted too, that's fine - counteract that by adjusting the time-stretch
			// amount more
			if (*phaseIncrement != pitchAdjustNeutralValue) {
				*timeStretchRatio = ((uint64_t)*timeStretchRatio * pitchAdjustNeutralValue) / (uint32_t)*phaseIncrement;
			}

			// Or if no manual pitch adjustment...
			else {

				// If we'd only be time-stretching a tiiiny bit (1/10th of an octave either direction)...
				if (*timeStretchRatio >= 15653696 && *timeStretchRatio < 17981375) {

					// And if we're less than 7.8mS out of sync...
					int32_t numSamplesLaggingBehindSync = guide->getNumSamplesLaggingBehindSync(voiceSample_);
					int32_t numSamplesDrift = std::abs(numSamplesLaggingBehindSync);

					if (numSamplesDrift < (((Sample*)guide->audioFileHolder->audioFile)->sampleRate >> 7)) {

						// We can just not time-stretch... for now
						*timeStretchRatio = kMaxSampleValue;
					}
				}
			}
		}

		// Or if pitch-stretch, achieve syncing that way
		else {

			// But first, if pitch was manually adjusted as well, counteract that by adjusting the time-stretch amount
			// more
			bool furtherPitchShifting = (*phaseIncrement != pitchAdjustNeutralValue);
			if (furtherPitchShifting) {
				*timeStretchRatio = ((uint64_t)pitchAdjustNeutralValue << 24) / (uint32_t)*phaseIncrement;
			}
			*phaseIncrement = (uint64_t)*phaseIncrement * (uint64_t)sampleLengthInSamples / *noteLengthInSamples;

			// If we're not time stretching / pitch shifting
			if (!furtherPitchShifting) {
				*phaseIncrement = guide->adjustPitchToCorrectDriftFromSync(voiceSample_, *phaseIncrement);
			}
		}
	}

	// Or if no syncing...
	else {

		*timeStretchRatio = getSpeedParamForNoSyncing(source, *phaseIncrement, pitchAdjustNeutralValue);
	}

	return true;
}

// This normally only gets called from the getPitchAndSpeedParams() function, above, but occasionally we'll also call it
// from render() when doing a "late start" on a Sample and we need to disregard any pitch modulation, so send this the
// non-modulated phaseIncrement.
uint32_t SampleSource::getSpeedParamForNoSyncing(::Source* source, int32_t phaseIncrement,
                                                 int32_t pitchAdjustNeutralValue) {

	uint32_t timeStretchRatio = kMaxSampleValue;

	// If pitch and time being treated independently, then achieve this by adjusting stretch to counteract pitch
	if (source->sampleControls.pitchAndSpeedAreIndependent) {
		if (phaseIncrement != pitchAdjustNeutralValue) {
			timeStretchRatio = ((uint64_t)pitchAdjustNeutralValue << 24) / (uint32_t)phaseIncrement;
		}
	}

	// And whether or not that was the case, if there's a manual adjustment to time-stretch, apply that now
	if (source->timeStretchAmount != 0) {
		timeStretchRatio = ((uint64_t)timeStretchRatio * timeStretchAdjustTable[source->timeStretchAmount + 48]) >> 24;
	}

	return timeStretchRatio;
}
