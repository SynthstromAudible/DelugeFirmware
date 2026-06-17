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

#include "model/voice/sample_engine.h"
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

bool SampleEngine::noteOn(VoiceSamplePlaybackGuide* guide, uint32_t samplesLate, int32_t priorityRating) {

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

void SampleEngine::unassign(bool deletingSong) {
	if (voiceSample_ != nullptr) {
		voiceSample_->beenUnassigned(deletingSong);
		AudioEngine::voiceSampleUnassigned(voiceSample_);
		voiceSample_ = nullptr;
	}
}

bool SampleEngine::cloneVoiceSampleFrom(const SampleEngine& from) {
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

bool SampleEngine::noteOffAtLoopEnd(Voice* voice, VoiceSamplePlaybackGuide* guide) {
	return voiceSample_->noteOffWhenLoopEndPointExists(voice, guide);
}

bool SampleEngine::zoneChanged(VoiceSamplePlaybackGuide* guide, Sample* sample, bool reversed, MarkerType markerType,
                               LoopType loopingType, int32_t priorityRating) {
	return voiceSample_->sampleZoneChanged(guide, sample, reversed, markerType, loopingType, priorityRating);
}

int32_t SampleEngine::getPlayByteLowLevel(Sample* sample, VoiceSamplePlaybackGuide* guide) {
	return voiceSample_->getPlayByteLowLevel(sample, guide);
}

deluge::synth::SourceRenderResult
SampleEngine::render(Voice& voice, ParamManagerForTimeline* paramManager, int32_t s, int32_t* __restrict__ oscBuffer,
                     int32_t numSamples, bool stereoBuffer, int32_t sourceAmplitude, int32_t amplitudeIncrement,
                     uint32_t phaseIncrement, uint32_t phaseIncrementStoredValue, bool stereoUnison, int32_t amplitudeL,
                     int32_t amplitudeR, int32_t* stereoUnisonScratch) {

	Sample* sample = (Sample*)voice.guides[s].audioFileHolder->audioFile;
	VoiceSample* voiceSample = voiceSample_;

	int32_t numChannels = (sample->numChannels == 2) ? 2 : 1;

#ifdef TEST_SAMPLE_LOOP_POINTS
	if (!(getNoise() >> 19)) {
		// D_PRINTLN("random change");

		int32_t r = getRandom255();

		if (r < 128) {
			voice.sound.guides[s].timeStretchAmount = (getRandom255() % 24) - 12;
		}

		else {
			if ((voice.guides[s].audioFileHolder->transpose || voice.guides[s].audioFileHolder->cents)
			    && getRandom255() < 128) {
				voice.guides[s].audioFileHolder->transpose = 0;
				voice.guides[s].audioFileHolder->setCents(0);
			}

			else {
				voice.guides[s].audioFileHolder->transpose = (int32_t)(getRandom255() % 24) - 12;
				voice.guides[s].audioFileHolder->setCents((int32_t)(getRandom255() % 100) - 50);
			}

			voice.sound.recalculateAllVoicePhaseIncrements(paramManager);
		}

		// D_PRINTLN("end random change");
	}
#endif

	// First figure out the time-stretching amount

	int32_t pitchAdjustNeutralValue = ((SampleHolder*)voice.guides[s].audioFileHolder)->neutralPhaseIncrement;
	uint32_t timeStretchRatio;
	uint32_t noteLengthInSamples;

	bool stillOk = getPitchAndSpeedParams(&voice.sound.sources[s], &voice.guides[s], &phaseIncrement, &timeStretchRatio,
	                                      &noteLengthInSamples);
	if (!stillOk) {
		return deluge::synth::SourceRenderResult::UNASSIGN;
	}

	// If user unmuted mid-note...
	bool tryToStartMidNote = voiceSample->pendingSamplesLate;
	if (tryToStartMidNote) [[unlikely]] {

		int32_t rawSamplesLate;

		// Synced / STRETCH - it's super easy.
		if (voice.sound.sources[s].repeatMode == SampleRepeatMode::STRETCH) {
			rawSamplesLate = voice.guides[s].getSyncedNumSamplesIn();
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
			uint32_t timeStretchRatioWithoutModulation =
			    getSpeedParamForNoSyncing(&voice.sound.sources[s], phaseIncrementStoredValue,
			                              ((SampleHolder*)voice.guides[s].audioFileHolder)->neutralPhaseIncrement);

			// Cool, so now we've got phaseIncrement and timeStretchRatio equivalent values which will indicate
			// our correct play position into the sample regardless of pitch modulation (almost always vibrato).
			rawSamplesLate = ((((uint64_t)voiceSample->pendingSamplesLate * phaseIncrementStoredValue) >> 24)
			                  * timeStretchRatioWithoutModulation)
			                 >> 24;
		}

		LateStartAttemptStatus result =
		    voiceSample->attemptLateSampleStart(&voice.guides[s], sample, rawSamplesLate, numSamples);

		if (result == LateStartAttemptStatus::FAILURE) {
			return deluge::synth::SourceRenderResult::UNASSIGN;
		}
		else if (result == LateStartAttemptStatus::WAIT) {
			return deluge::synth::SourceRenderResult::SKIP;
		}
		// Otherwise, it started fine!
	}

	LoopType loopingType = voice.guides[s].getLoopingType(voice.sound.sources[s]);

	int32_t interpolationBufferSize;

	// If pitch adjustment...
	if (phaseIncrement != kMaxSampleValue) {

		// Work out what quality we're going to do that at
		interpolationBufferSize = voice.sound.sources[s].sampleControls.getInterpolationBufferSize(phaseIncrement);

		// And if first render, and other conditions met, see if we can use cache.
		// It may seem like it'd be a good idea to try and set this up on note-on, rather than here in the
		// rendering routine, but I tried that and the fact is that it means a bunch of extra computation has to
		// happen to work out pitch and timestretch there as well as here (where it'll be worked out anyway),
		// including checking the result of patching / modulation (and we *do* allow caching where, say,
		// velocity or note is affecting pitch), and stretch-syncing.
		if (!voiceSample->doneFirstRenderYet && !tryToStartMidNote
		    && voice.portaEnvelopePos == 0xFFFFFFFF) { // No porta

			// If looping, make sure the loop isn't too short. If so, caching just wouldn't sound good /
			// accurate
			if (loopingType != LoopType::NONE) {
				SampleHolderForVoice* holder = (SampleHolderForVoice*)voice.guides[s].audioFileHolder;
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

			// If no changeable sources patched to pitch...
			for (int32_t c = 0; c < paramManager->getPatchCableSet()->numUsablePatchCables; c++) {
				PatchCable* cable = &paramManager->getPatchCableSet()->patchCables[c];

				// If it's going to pitch...
				if (cable->destinationParamDescriptor.isSetToParamWithNoSource(params::LOCAL_PITCH_ADJUST)
				    || cable->destinationParamDescriptor.isSetToParamWithNoSource(params::LOCAL_OSC_A_PITCH_ADJUST
				                                                                  + s)) {

					// And if it's an envelope or LFO or random...
					if (cable->from == PatchSource::ENVELOPE_0 || cable->from == PatchSource::ENVELOPE_1
					    || cable->from == PatchSource::LFO_GLOBAL_1 || cable->from == PatchSource::LFO_LOCAL_1
					    || cable->from == PatchSource::LFO_GLOBAL_2 || cable->from == PatchSource::LFO_LOCAL_2
					    || cable->from == PatchSource::RANDOM) {
						goto dontUseCache;
					}

					else if (cable->from == PatchSource::AFTERTOUCH) {
						if (voice.params.sourceValues[util::to_underlying(PatchSource::AFTERTOUCH)]) {
							goto dontUseCache;
						}
					}

					// TODO: probably need to check for X and Y modulation sources here too...

					else if (cable->from == PatchSource::SIDECHAIN) {
						if (voice.sound.params.sourceValues[util::to_underlying(PatchSource::SIDECHAIN)]) {
							goto dontUseCache;
						}
					}
				}
			}

			{
				// If still here, we can use cache
				bool everythingOk = voiceSample->possiblySetUpCache(&voice.sound.sources[s].sampleControls,
				                                                    &voice.guides[s], phaseIncrement, timeStretchRatio,
				                                                    voice.getPriorityRating(), loopingType);
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

	bool stillActive = voiceSample->render(
	    &voice.guides[s], renderBuffer, numSamples, sample, numChannels, loopingType, phaseIncrement, timeStretchRatio,
	    sourceAmplitude, amplitudeIncrement, interpolationBufferSize,
	    voice.sound.sources[s].sampleControls.interpolationMode, voice.getPriorityRating());

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

bool SampleEngine::getPitchAndSpeedParams(Source* source, VoiceSamplePlaybackGuide* guide, uint32_t* phaseIncrement,
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
uint32_t SampleEngine::getSpeedParamForNoSyncing(Source* source, int32_t phaseIncrement,
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
