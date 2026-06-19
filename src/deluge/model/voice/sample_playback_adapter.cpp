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

#include "model/voice/sample_playback_adapter.h"
#include "model/sample/sample.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/voice/voice_sample.h"
#include "model/voice/voice_sample_playback_guide.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/source.h"

// --- App-side note lifecycle --------------------------------------------------------------------------------

bool SamplePlaybackAdapter::noteOn(::Source* source, VoiceSamplePlaybackGuide* guide, uint32_t samplesLate,
                                   int32_t priorityRating) {

	// Cache the per-note source config + guide (stable for the note); the render queries read these.
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

void SamplePlaybackAdapter::unassign(bool deletingSong) {
	if (voiceSample_ != nullptr) {
		voiceSample_->beenUnassigned(deletingSong);
		AudioEngine::voiceSampleUnassigned(voiceSample_);
		voiceSample_ = nullptr;
	}
	source_ = nullptr;
	guide_ = nullptr;
}

bool SamplePlaybackAdapter::cloneVoiceSampleFrom(const SamplePlaybackAdapter& from) {
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

bool SamplePlaybackAdapter::noteOffAtLoopEnd(Voice* voice, VoiceSamplePlaybackGuide* guide) {
	return voiceSample_->noteOffWhenLoopEndPointExists(voice, guide);
}

bool SamplePlaybackAdapter::zoneChanged(VoiceSamplePlaybackGuide* guide, Sample* sample, bool reversed,
                                        MarkerType markerType, LoopType loopingType, int32_t priorityRating) {
	return voiceSample_->sampleZoneChanged(guide, sample, reversed, markerType, loopingType, priorityRating);
}

int32_t SamplePlaybackAdapter::getPlayByteLowLevel(Sample* sample, VoiceSamplePlaybackGuide* guide) {
	return voiceSample_->getPlayByteLowLevel(sample, guide);
}

// --- deluge::synth::SamplePlaybackPort -----------------------------------------------------------------------

int32_t SamplePlaybackAdapter::numChannels() const {
	return (((Sample*)guide_->audioFileHolder->audioFile)->numChannels == 2) ? 2 : 1;
}

int32_t SamplePlaybackAdapter::sampleRate() const {
	return ((Sample*)guide_->audioFileHolder->audioFile)->sampleRate;
}

int32_t SamplePlaybackAdapter::neutralPhaseIncrement() const {
	return ((SampleHolder*)guide_->audioFileHolder)->neutralPhaseIncrement;
}

int32_t SamplePlaybackAdapter::sampleLengthAtSystemSampleRate() const {
	return ((SampleHolder*)guide_->audioFileHolder)->getLengthInSamplesAtSystemSampleRate(true);
}

bool SamplePlaybackAdapter::isStretchRepeatMode() const {
	return source_->repeatMode == SampleRepeatMode::STRETCH;
}

bool SamplePlaybackAdapter::pitchAndSpeedIndependent() const {
	return source_->sampleControls.pitchAndSpeedAreIndependent;
}

int32_t SamplePlaybackAdapter::timeStretchAmount() const {
	return source_->timeStretchAmount;
}

uint32_t SamplePlaybackAdapter::sequenceSyncLengthTicks() const {
	return guide_->sequenceSyncLengthTicks;
}

LoopType SamplePlaybackAdapter::loopingType() const {
	return guide_->getLoopingType(*source_);
}

SamplePlaybackAdapter::LoopPoints SamplePlaybackAdapter::loopPoints() const {
	auto* holder = (SampleHolderForVoice*)guide_->audioFileHolder;
	int32_t start = holder->loopStartPos ? holder->loopStartPos : holder->startPos;
	int32_t end = holder->loopEndPos ? holder->loopEndPos : holder->endPos;
	return {.start = start, .end = end};
}

uint64_t SamplePlaybackAdapter::timePerInternalTickBig() const {
	return playbackHandler.getTimePerInternalTickBig();
}

int64_t SamplePlaybackAdapter::syncedNumSamplesIn() const {
	return guide_->getSyncedNumSamplesIn();
}

int32_t SamplePlaybackAdapter::numSamplesLaggingBehindSync() const {
	return guide_->getNumSamplesLaggingBehindSync(voiceSample_);
}

uint32_t SamplePlaybackAdapter::adjustPitchToCorrectDriftFromSync(uint32_t phaseIncrement) const {
	return guide_->adjustPitchToCorrectDriftFromSync(voiceSample_, phaseIncrement);
}

int32_t SamplePlaybackAdapter::interpolationBufferSize(uint32_t phaseIncrement) const {
	return source_->sampleControls.getInterpolationBufferSize(phaseIncrement);
}

uint32_t SamplePlaybackAdapter::pendingSamplesLate() const {
	return voiceSample_->pendingSamplesLate;
}

bool SamplePlaybackAdapter::doneFirstRenderYet() const {
	return voiceSample_->doneFirstRenderYet;
}

SamplePlaybackAdapter::LateStartOutcome SamplePlaybackAdapter::attemptLateStart(int64_t rawSamplesLate,
                                                                                int32_t numSamples) {
	Sample* sample = (Sample*)guide_->audioFileHolder->audioFile;
	switch (voiceSample_->attemptLateSampleStart(guide_, sample, rawSamplesLate, numSamples)) {
	case LateStartAttemptStatus::SUCCESS:
		return LateStartOutcome::OK;
	case LateStartAttemptStatus::WAIT:
		return LateStartOutcome::WAIT;
	default:
		return LateStartOutcome::FAILURE;
	}
}

bool SamplePlaybackAdapter::possiblySetUpCache(int32_t phaseIncrement, int32_t timeStretchRatio, int32_t priorityRating,
                                               LoopType loopingType) {
	return voiceSample_->possiblySetUpCache(&source_->sampleControls, guide_, phaseIncrement, timeStretchRatio,
	                                        priorityRating, loopingType);
}

bool SamplePlaybackAdapter::render(int32_t* buffer, int32_t numSamples, LoopType loopingType, int32_t phaseIncrement,
                                   int32_t timeStretchRatio, int32_t sourceAmplitude, int32_t amplitudeIncrement,
                                   int32_t interpolationBufferSize, int32_t priorityRating) {
	Sample* sample = (Sample*)guide_->audioFileHolder->audioFile;
	int32_t numChannels = (sample->numChannels == 2) ? 2 : 1;
	return voiceSample_->render(guide_, buffer, numSamples, sample, numChannels, loopingType, phaseIncrement,
	                            timeStretchRatio, sourceAmplitude, amplitudeIncrement, interpolationBufferSize,
	                            source_->sampleControls.interpolationMode, priorityRating);
}
