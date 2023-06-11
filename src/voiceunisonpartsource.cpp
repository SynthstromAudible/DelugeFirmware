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

#include <AudioEngine.h>
#include <voicesampleplaybackguide.h>
#include "voiceunisonpartsource.h"
#include "SampleCache.h"
#include "voice.h"
#include "source.h"
#include "VoiceSample.h"
#include "MultisampleRange.h"
#include "GeneralMemoryAllocator.h"
#include "playbackhandler.h"
#include "song.h"

VoiceUnisonPartSource::VoiceUnisonPartSource() {
	voiceSample = NULL;
	livePitchShifter = NULL;
}

bool VoiceUnisonPartSource::noteOn(Voice* voice, Source* source, VoiceSamplePlaybackGuide* guide, uint32_t samplesLate,
                                   uint32_t oscRetriggerPhase, bool resetEverything, uint8_t synthMode) {

	if (synthMode != SYNTH_MODE_FM && source->oscType == OSC_TYPE_SAMPLE) {

		if (!guide->audioFileHolder || !guide->audioFileHolder->audioFile
		    || ((Sample*)guide->audioFileHolder->audioFile)->unplayable)
			return true; // We didn't succeed, but don't want to stop the whole Voice from sounding necessarily

		if (!voiceSample) { // We might actually already have one, and just be restarting this voice
			voiceSample = AudioEngine::solicitVoiceSample();
			if (!voiceSample) return false;
		}
		voiceSample->noteOn(guide, samplesLate, voice->getPriorityRating());
		if (samplesLate) return true; // We're finished in this case
		return voiceSample->setupClusersForInitialPlay(guide, (Sample*)guide->audioFileHolder->audioFile, 0, false, 1);
	}

	if (synthMode != SYNTH_MODE_FM
	    && (source->oscType == OSC_TYPE_SAMPLE || source->oscType == OSC_TYPE_INPUT_L
	        || source->oscType == OSC_TYPE_INPUT_R || source->oscType == OSC_TYPE_INPUT_STEREO)) {
		//oscPos = 0;
	}
	else {
		if (oscRetriggerPhase != 0xFFFFFFFF) oscPos = getOscInitialPhaseForZero(source->oscType) + oscRetriggerPhase;
	}

	if (resetEverything) carrierFeedback = 0;

	return true;
}

void VoiceUnisonPartSource::unassign() {
	active = false;
	if (voiceSample) {
		voiceSample->beenUnassigned();
		AudioEngine::voiceSampleUnassigned(voiceSample);
		voiceSample = NULL;
	}

	if (livePitchShifter) {
		generalMemoryAllocator.dealloc(livePitchShifter);
		livePitchShifter = NULL;
	}
}

bool VoiceUnisonPartSource::getPitchAndSpeedParams(Source* source, VoiceSamplePlaybackGuide* guide,
                                                   uint32_t* phaseIncrement, uint32_t* timeStretchRatio,
                                                   uint32_t* noteLengthInSamples) {

	int32_t pitchAdjustNeutralValue = ((SampleHolder*)guide->audioFileHolder)->neutralPhaseIncrement;

	// If syncing...
	if (guide->sequenceSyncLengthTicks) {

		*timeStretchRatio = 16777216;

		uint32_t sampleLengthInSamples =
		    ((SampleHolder*)guide->audioFileHolder)
		        ->getLengthInSamplesAtSystemSampleRate(true); // That is, after converted to 44.1kHz
		*noteLengthInSamples = (playbackHandler.getTimePerInternalTickBig() * guide->sequenceSyncLengthTicks)
		                       >> 32; // No rounding. Should be fine?

		// To stop things getting insane, limit to 32x speed
		if ((sampleLengthInSamples >> 5) > *noteLengthInSamples) return false;

		// If time stretch, achieve syncing that way
		if (source->sampleControls.pitchAndSpeedAreIndependent) {
			*timeStretchRatio = (uint64_t)16777216 * (uint64_t)sampleLengthInSamples / *noteLengthInSamples;

			// And if pitch was manually adjusted too, that's fine - counteract that by adjusting the time-stretch amount more
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
						*timeStretchRatio = 16777216;
					}
				}
			}
		}

		// Or if pitch-stretch, achieve syncing that way
		else {

			// But first, if pitch was manually adjusted as well, counteract that by adjusting the time-stretch amount more
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

// This normally only gets called from the getPitchAndSpeedParams() function, above, but occasionally we'll also call it from Voice::renderBasicSource() when doing a
// "late start" on a Sample and we need to disregard any pitch modulation, so send this the non-modulated phaseIncrement.
uint32_t VoiceUnisonPartSource::getSpeedParamForNoSyncing(Source* source, int32_t phaseIncrement,
                                                          int32_t pitchAdjustNeutralValue) {

	uint32_t timeStretchRatio = 16777216;

	// If pitch and time being treated independently, then achieve this by adjusting stretch to counteract pitch
	if (source->sampleControls.pitchAndSpeedAreIndependent) {
		if (phaseIncrement != pitchAdjustNeutralValue) {
			timeStretchRatio = ((uint64_t)pitchAdjustNeutralValue << 24) / (uint32_t)phaseIncrement;
		}
	}

	// And whether or not that was the case, if there's a manual adjustment to time-stretch, apply that now
	if (source->timeStretchAmount) {
		timeStretchRatio = ((uint64_t)timeStretchRatio * timeStretchAdjustTable[source->timeStretchAmount + 48]) >> 24;
	}

	return timeStretchRatio;
}
