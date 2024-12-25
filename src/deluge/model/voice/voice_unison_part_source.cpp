/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "model/voice/voice_unison_part_source.h"
#include "dsp/dx/dx7note.h"
#include "dsp/dx/engine.h"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample_cache.h"
#include "model/song/song.h"
#include "model/voice/voice.h"
#include "model/voice/voice_sample.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/source.h"
#include "storage/multi_range/multisample_range.h"

bool VoiceUnisonPartSource::noteOn(Voice* voice, Source* source, VoiceSamplePlaybackGuide* guide, uint32_t samplesLate,
                                   uint32_t oscRetriggerPhase, bool resetEverything, SynthMode synthMode,
                                   uint8_t velocity) {

	if (synthMode != SynthMode::FM && source->oscType == OscType::SAMPLE) {

		if ((guide->audioFileHolder == nullptr) || (guide->audioFileHolder->audioFile == nullptr)
		    || ((Sample*)guide->audioFileHolder->audioFile)->unplayable) {
			return true; // We didn't succeed, but don't want to stop the whole Voice from sounding necessarily
		}

		if (voiceSample == nullptr) { // We might actually already have one, and just be restarting this voice
			voiceSample = AudioEngine::solicitVoiceSample();
			if (voiceSample == nullptr) [[unlikely]] {
				return false;
			}
		}
		else {
			// if we're restarting a voice we need to clear it's reasons,
			// otherwise we'll increase now but only reduce by one at note off
			// not quite thread safe - if the sample is shorter than 64k
			// an allocation before setupClustersForInitialPlay could steal it
			voiceSample->beenUnassigned(false);
		}
		voiceSample->noteOn(guide, samplesLate, voice->getPriorityRating());
		if (samplesLate != 0u) {
			return true; // We're finished in this case
		}
		return voiceSample->setupClusersForInitialPlay(guide, (Sample*)guide->audioFileHolder->audioFile, 0, false, 1);
	}

	if (synthMode != SynthMode::FM
	    && (source->oscType == OscType::SAMPLE || source->oscType == OscType::INPUT_L
	        || source->oscType == OscType::INPUT_R || source->oscType == OscType::INPUT_STEREO)) {
		// oscPos = 0;
	}
	else if (synthMode != SynthMode::FM && source->oscType == OscType::DX7) [[unlikely]] {
		if (dxVoice == nullptr) { // We might actually already have one, and just be restarting this voice
			dxVoice = getDxEngine()->solicitDxVoice();
			if (dxVoice == nullptr) {
				return false;
			}
		}

		DxPatch* patch = source->ensureDxPatch();
		dxVoice->init(*patch, voice->noteCodeAfterArpeggiation, velocity);
	}
	else {
		if (oscRetriggerPhase != 0xFFFFFFFF) {
			oscPos = getOscInitialPhaseForZero(source->oscType) + oscRetriggerPhase;
		}
	}

	if (resetEverything) {
		carrierFeedback = 0;
	}

	return true;
}

void VoiceUnisonPartSource::unassign(bool deletingSong) {
	active = false;
	if (voiceSample != nullptr) {
		voiceSample->beenUnassigned(deletingSong);
		AudioEngine::voiceSampleUnassigned(voiceSample);
		voiceSample = nullptr;
	}

	if (dxVoice != nullptr) {
		dxEngine->dxVoiceUnassigned(dxVoice);
		dxVoice = NULL;
	}

	if (livePitchShifter != nullptr) {
		delugeDealloc(livePitchShifter);
		livePitchShifter = nullptr;
	}
}

bool VoiceUnisonPartSource::getPitchAndSpeedParams(Source* source, VoiceSamplePlaybackGuide* guide,
                                                   uint32_t* phaseIncrement, uint32_t* timeStretchRatio,
                                                   uint32_t* noteLengthInSamples) {

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
					int32_t numSamplesLaggingBehindSync = guide->getNumSamplesLaggingBehindSync(voiceSample);
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
				*phaseIncrement = guide->adjustPitchToCorrectDriftFromSync(voiceSample, *phaseIncrement);
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
// from Voice::renderBasicSource() when doing a "late start" on a Sample and we need to disregard any pitch modulation,
// so send this the non-modulated phaseIncrement.
uint32_t VoiceUnisonPartSource::getSpeedParamForNoSyncing(Source* source, int32_t phaseIncrement,
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
