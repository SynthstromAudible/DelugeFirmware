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

#include "synth/source/sample_source.h"
#include "definitions_cxx.hpp"
#include "util/fixedpoint.h"
#include "util/lookuptables/lookuptables.h" // timeStretchAdjustTable
#include <cstdint>
#include <cstdlib> // std::abs
#include <cstring> // memset

namespace deluge::synth {

SourceRenderResult SampleSource::renderSource(const SourceRenderContext& ctx) {
	int32_t* __restrict__ oscBuffer = ctx.oscBuffer;
	const int32_t numSamples = ctx.numSamples;
	const int32_t sourceAmplitude = ctx.sourceAmplitude;
	const int32_t amplitudeIncrement = ctx.amplitudeIncrement;
	uint32_t phaseIncrement = ctx.phaseIncrement; // mutated below by getPitchAndSpeedParams (taken by address)
	const uint32_t phaseIncrementStoredValue = ctx.phaseIncrementStoredValue;
	const bool stereoUnison = ctx.stereoUnison;
	const int32_t amplitudeL = ctx.amplitudeL;
	const int32_t amplitudeR = ctx.amplitudeR;
	int32_t* stereoUnisonScratch = ctx.stereoScratch;

	const int32_t numChannels = port_->numChannels();

	// (The legacy TEST_SAMPLE_LOOP_POINTS fuzzing block was removed here: it mutated voice/sound state via
	// Voice&/paramManager, incompatible with the synth::Source contract. A debug-only #ifdef, never in normal or
	// golden builds; reintroduce app-side if wanted.)

	// First figure out the time-stretching amount.
	uint32_t timeStretchRatio;
	uint32_t noteLengthInSamples;

	bool stillOk = getPitchAndSpeedParams(&phaseIncrement, &timeStretchRatio, &noteLengthInSamples);
	if (!stillOk) {
		return SourceRenderResult::UNASSIGN;
	}

	// If user unmuted mid-note...
	bool tryToStartMidNote = port_->pendingSamplesLate();
	if (tryToStartMidNote) [[unlikely]] {

		int32_t rawSamplesLate;

		// Synced / STRETCH - it's super easy.
		if (port_->isStretchRepeatMode()) {
			rawSamplesLate = port_->syncedNumSamplesIn();
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
			    getSpeedParamForNoSyncing(phaseIncrementStoredValue, port_->neutralPhaseIncrement());

			// Cool, so now we've got phaseIncrement and timeStretchRatio equivalent values which will indicate
			// our correct play position into the sample regardless of pitch modulation (almost always vibrato).
			rawSamplesLate = ((((uint64_t)port_->pendingSamplesLate() * phaseIncrementStoredValue) >> 24)
			                  * timeStretchRatioWithoutModulation)
			                 >> 24;
		}

		switch (port_->attemptLateStart(rawSamplesLate, numSamples)) {
		case SamplePlaybackPort::LateStartOutcome::FAILURE:
			return SourceRenderResult::UNASSIGN;
		case SamplePlaybackPort::LateStartOutcome::WAIT:
			return SourceRenderResult::SKIP;
		case SamplePlaybackPort::LateStartOutcome::OK:
			break; // it started fine!
		}
	}

	LoopType loopingType = port_->loopingType();

	int32_t interpolationBufferSize;

	// If pitch adjustment...
	if (phaseIncrement != kMaxSampleValue) {

		// Work out what quality we're going to do that at
		interpolationBufferSize = port_->interpolationBufferSize(phaseIncrement);

		// And if first render, and other conditions met, see if we can use cache.
		// It may seem like it'd be a good idea to try and set this up on note-on, rather than here in the
		// rendering routine, but I tried that and the fact is that it means a bunch of extra computation has to
		// happen to work out pitch and timestretch there as well as here (where it'll be worked out anyway),
		// including checking the result of patching / modulation (and we *do* allow caching where, say,
		// velocity or note is affecting pitch), and stretch-syncing.
		if (!port_->doneFirstRenderYet() && !tryToStartMidNote && ctx.portaEnvelopePos == 0xFFFFFFFF) { // No porta

			// If looping, make sure the loop isn't too short. If so, caching just wouldn't sound good /
			// accurate
			if (loopingType != LoopType::NONE) {
				SamplePlaybackPort::LoopPoints loop = port_->loopPoints();

				int32_t loopLength = loop.end - loop.start;
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
				bool everythingOk =
				    port_->possiblySetUpCache(phaseIncrement, timeStretchRatio, ctx.priorityRating, loopingType);
				if (!everythingOk) {
					return SourceRenderResult::UNASSIGN;
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

	bool stillActive = port_->render(renderBuffer, numSamples, loopingType, phaseIncrement, timeStretchRatio,
	                                 sourceAmplitude, amplitudeIncrement, interpolationBufferSize, ctx.priorityRating);

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
		return SourceRenderResult::UNASSIGN;
	}
	return SourceRenderResult::ACTIVE;
}

bool SampleSource::getPitchAndSpeedParams(uint32_t* phaseIncrement, uint32_t* timeStretchRatio,
                                          uint32_t* noteLengthInSamples) {

	int32_t pitchAdjustNeutralValue = port_->neutralPhaseIncrement();

	// If syncing...
	if (port_->sequenceSyncLengthTicks()) {

		*timeStretchRatio = kMaxSampleValue;

		// That is, after converted to 44.1kHz
		uint32_t sampleLengthInSamples = port_->sampleLengthAtSystemSampleRate();
		*noteLengthInSamples =
		    (port_->timePerInternalTickBig() * port_->sequenceSyncLengthTicks()) >> 32; // No rounding. Should be fine?

		// To stop things getting insane, limit to 32x speed
		if ((sampleLengthInSamples >> 5) > *noteLengthInSamples) {
			return false;
		}

		// If time stretch, achieve syncing that way
		if (port_->pitchAndSpeedIndependent()) {
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
					int32_t numSamplesLaggingBehindSync = port_->numSamplesLaggingBehindSync();
					int32_t numSamplesDrift = std::abs(numSamplesLaggingBehindSync);

					if (numSamplesDrift < (port_->sampleRate() >> 7)) {

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
				*phaseIncrement = port_->adjustPitchToCorrectDriftFromSync(*phaseIncrement);
			}
		}
	}

	// Or if no syncing...
	else {

		*timeStretchRatio = getSpeedParamForNoSyncing(*phaseIncrement, pitchAdjustNeutralValue);
	}

	return true;
}

// This normally only gets called from the getPitchAndSpeedParams() function, above, but occasionally we'll also call it
// from render() when doing a "late start" on a Sample and we need to disregard any pitch modulation, so send this the
// non-modulated phaseIncrement.
uint32_t SampleSource::getSpeedParamForNoSyncing(int32_t phaseIncrement, int32_t pitchAdjustNeutralValue) {

	uint32_t timeStretchRatio = kMaxSampleValue;

	// If pitch and time being treated independently, then achieve this by adjusting stretch to counteract pitch
	if (port_->pitchAndSpeedIndependent()) {
		if (phaseIncrement != pitchAdjustNeutralValue) {
			timeStretchRatio = ((uint64_t)pitchAdjustNeutralValue << 24) / (uint32_t)phaseIncrement;
		}
	}

	// And whether or not that was the case, if there's a manual adjustment to time-stretch, apply that now
	if (port_->timeStretchAmount() != 0) {
		timeStretchRatio = ((uint64_t)timeStretchRatio * timeStretchAdjustTable[port_->timeStretchAmount() + 48]) >> 24;
	}

	return timeStretchRatio;
}

} // namespace deluge::synth
