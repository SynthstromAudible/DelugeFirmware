/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "model/clip/audio_clip.h"
#include "clip.h"
#include "definitions_cxx.hpp"
#include "dsp/timestretch/time_stretcher.h"
#include "gui/views/automation_view.h"
#include "gui/waveform/waveform_renderer.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/clip_instance.h"
#include "model/consequence/consequence_output_existence.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "model/voice/voice_sample.h"
#include "modulation/params/param_set.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/fixedpoint.h"
#include <new>

namespace params = deluge::modulation::params;

extern "C" {
#include "RZA1/uart/sio_char.h"
extern uint8_t currentlyAccessingCard;
}

AudioClip::AudioClip() : Clip(ClipType::AUDIO) {
	overdubsShouldCloneOutput = true;
	voiceSample = nullptr;
	guide.audioFileHolder = &sampleHolder; // It needs to permanently point here

	sampleControls.pitchAndSpeedAreIndependent = true;

	recorder = nullptr;

	renderData.xScroll = -1;

	voicePriority = VoicePriority::MEDIUM;
	attack = -2147483648;
}

AudioClip::~AudioClip() {
	if (recorder) {
		FREEZE_WITH_ERROR("E278");
	}

	// Sirhc actually got this in a V3.0.5 RC! No idea how. Also Qui got around V3.1.3.
	// I suspect that recorder is somehow still set when this Clip gets "deleted" by being put in a
	// ConsequenceClipExistence. Somehow neither abortRecording() nor finishLinearRecording() would be being called when
	// some other AudioClip becomes the Output's activeClip. These get called in slightly roundabout ways, so this seems
	// the most likely. I've added some further error code diversification.
}

// Will replace the Clip in the modelStack, if success.
Error AudioClip::clone(ModelStackWithTimelineCounter* modelStack, bool shouldFlattenReversing) const {

	void* clipMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(AudioClip));
	if (!clipMemory) {
		return Error::INSUFFICIENT_RAM;
	}

	auto newClip = new (clipMemory) AudioClip();

	newClip->copyBasicsFrom(this);
	Error error = newClip->paramManager.cloneParamCollectionsFrom(&paramManager, true);
	if (error != Error::NONE) {
		newClip->~AudioClip();
		delugeDealloc(clipMemory);
		return error;
	}

	modelStack->setTimelineCounter(newClip);

	newClip->activeIfNoSolo = false;
	newClip->soloingInSessionMode = false;
	newClip->output = output;

	newClip->attack = attack;

	memcpy(&newClip->sampleControls, &sampleControls, sizeof(sampleControls));
	newClip->voicePriority = voicePriority;

	newClip->sampleHolder.beenClonedFrom(&sampleHolder, sampleControls.isCurrentlyReversed());

	return Error::NONE;
}

void AudioClip::copyBasicsFrom(Clip const* otherClip) {
	Clip::copyBasicsFrom(otherClip);
	overdubsShouldCloneOutput = ((AudioClip*)otherClip)->overdubsShouldCloneOutput;
}

void AudioClip::abortRecording() {

	if (!recorder) {
		return; // This is allowed to happen
	}

	renderData.xScroll = -1; // Force re-render

	recorder->pointerHeldElsewhere = false; // Must do before calling recorder->abort()

	recorder->abort();

	recorder = nullptr;

	session.justAbortedSomeLinearRecording();

	if (getRootUI()) {
		getRootUI()->clipNeedsReRendering(this);
	}

	actionLogger.notifyClipRecordingAborted(this);
}

bool AudioClip::wantsToBeginLinearRecording(Song* song) {
	return (Clip::wantsToBeginLinearRecording(song) && (!sampleHolder.audioFile || !shouldCloneForOverdubs())
	        && ((AudioOutput*)output)->inputChannel > AudioInputChannel::NONE);
}

bool AudioClip::isAbandonedOverdub() {
	return (isUnfinishedAutoOverdub && !sampleHolder.audioFile);
}

Error AudioClip::beginLinearRecording(ModelStackWithTimelineCounter* modelStack, int32_t buttonPressLatency) {

	AudioInputChannel inputChannel;
	Output* outputRecordingFrom;
	int32_t numChannels;
	bool shouldNormalize{false};
	if (isEmpty()) {

		inputChannel = ((AudioOutput*)output)->inputChannel;
		outputRecordingFrom = ((AudioOutput*)output)->getOutputRecordingFrom();
		numChannels =
		    (inputChannel >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION || inputChannel == AudioInputChannel::STEREO)
		        ? 2
		        : 1;
		shouldNormalize =
		    (inputChannel < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION); // if reading from input we need this
	}
	// if we already have an input then we're going to record an overdub instead. This won't be called if the clip is
	// doing the classic deluge clip cloning loops (will be called on the clone instead)
	else {
		inputChannel = AudioInputChannel::SPECIFIC_OUTPUT;
		outputRecordingFrom = output;
		numChannels = 2;
	}
	bool shouldRecordMarginsNow =
	    FlashStorage::audioClipRecordMargins && inputChannel < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION;

	recorder = AudioEngine::getNewRecorder(numChannels, AudioRecordingFolder::CLIPS, inputChannel, true,
	                                       shouldRecordMarginsNow, buttonPressLatency, false, outputRecordingFrom);
	if (!recorder) {
		return Error::INSUFFICIENT_RAM;
	}
	recorder->autoDeleteWhenDone = true;
	recorder->allowNormalization = shouldNormalize;
	return Clip::beginLinearRecording(modelStack, buttonPressLatency);
}

void AudioClip::finishLinearRecording(ModelStackWithTimelineCounter* modelStack, Clip* nextPendingOverdub,
                                      int32_t buttonLatencyForTempolessRecord) {
	if (!recorder) {
		return; // Shouldn't ever happen?
	}

	// Got to check reachedMaxFileSize here, cos that'll go true a bit before cardRoutine() sets status to ERROR
	// also check if we haven't captured any samples (which can happen with threshold recording)
	if (recorder->status == RecorderStatus::ABORTED || recorder->reachedMaxFileSize || !recorder->numSamplesCaptured) {
		abortRecording();
		return;
	}

	// Have to do this before setting currentlyRecordingLinearly to false, for vertical scroll reasons
	Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);

	if (!isUnfinishedAutoOverdub) {

		if (action) {
			// Must happen before sampleHolder.filePath.set()
			action->recordAudioClipSampleChange(this);
		}
	}

	recorder->pointerHeldElsewhere = false;

	recorder->endSyncedRecording(buttonLatencyForTempolessRecord); // Must call before setSample(), cos it sets up
	                                                               // important stuff like the sample length

	// SampleRecorder will also call sampleNeedsReRendering() when "capturing" is finished, but in plenty of cases, that
	// will have happened in the above call to endSyncedRecording(), and our sample hasn't been set yet, so that won't
	// have been effective. So, we have to call this here too, to cover our bases.
	if (getRootUI()) {
		getRootUI()->clipNeedsReRendering(this);
	}

	if (!isEmpty()) {
		clear(nullptr, modelStack, true, true);
	}
	auto ao = (AudioOutput*)output;
	// if we're a sampler then turn off monitoring
	// if (ao->mode == AudioOutputMode::sampler) {
	// 	ao->mode = AudioOutputMode::player;
	// }
	originalLength = loopLength;
	sampleHolder.filePath.set(&recorder->sample->filePath);
	sampleHolder.setAudioFile(recorder->sample, sampleControls.isCurrentlyReversed(), true,
	                          CLUSTER_DONT_LOAD); // Adds a reason to the first Cluster(s). Must call this after
	                                              // endSyncedRecording(), which puts some final values in the Sample

	renderData.xScroll = -1; // Force re-render - though this would surely happen anyway

	if (recorder->recordingExtraMargins) {
		attack = kAudioClipDefaultAttackIfPreMargin; // TODO: make these undoable?
	}

	isUnfinishedAutoOverdub = false;

	recorder = nullptr;

	name.set(sampleHolder.filePath.get());
}

Clip* AudioClip::cloneAsNewOverdub(ModelStackWithTimelineCounter* modelStackOldClip, OverDubType newOverdubNature) {
	// Allocate memory for audio clip
	void* clipMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(AudioClip));
	if (!clipMemory) {
ramError:
		display->displayError(Error::INSUFFICIENT_RAM);
		return nullptr;
	}

	AudioClip* newClip = new (clipMemory) AudioClip();

	newClip->setupForRecordingAsAutoOverdub(this, modelStackOldClip->song, newOverdubNature);

	char modelStackMemoryNewClip[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackNewClip =
	    setupModelStackWithTimelineCounter(modelStackMemoryNewClip, modelStackOldClip->song, newClip);

	Error error = newClip->setOutput(modelStackNewClip, output, this);

	if (error != Error::NONE) {
		newClip->~AudioClip();
		delugeDealloc(clipMemory);
		goto ramError;
	}

#if ALPHA_OR_BETA_VERSION
	if (!newClip->paramManager.summaries[0].paramCollection) {
		FREEZE_WITH_ERROR("E421"); // Trying to diversify Leo's E410
	}
#endif

	return newClip;
}

bool AudioClip::cloneOutput(ModelStackWithTimelineCounter* modelStack) {
	// don't clone for loop commands in red mode
	if (!overdubsShouldCloneOutput) {
		return false;
	}

	AudioOutput* newOutput = modelStack->song->createNewAudioOutput();
	if (!newOutput) {
		return false;
	}

	newOutput->cloneFrom((AudioOutput*)output);

	newOutput->wasCreatedForAutoOverdub = true;
	changeOutput(modelStack, newOutput);

	return true;
}

void AudioClip::processCurrentPos(ModelStackWithTimelineCounter* modelStack, uint32_t ticksSinceLast) {

	Clip::processCurrentPos(modelStack, ticksSinceLast);
	if (modelStack->getTimelineCounter() != this) {
		return;
	}

	// If we have a recorder that's gotten into error/aborted state, but we haven't registered that here yet, do that
	// now. This isn't really the ideal place for this...
	if (recorder && recorder->status == RecorderStatus::ABORTED) {
		abortRecording();
	}

	// If at pos 0, that's the only place where anything really important happens: play the Sample
	// also play it if we're auto extending and we just did that
	if (!lastProcessedPos || lastProcessedPos == nextSampleRestartPos) {
		// original length is only 0 when recording into arranger, in which case we don't want to loop
		if (getCurrentlyRecordingLinearly() && originalLength != 0) {
			nextSampleRestartPos = lastProcessedPos + originalLength;
			// make sure we come back here later
			if (originalLength < playbackHandler.swungTicksTilNextEvent) {
				playbackHandler.swungTicksTilNextEvent = originalLength;
			}
		}
		// If there is a sample, play it
		if (sampleHolder.audioFile && !((Sample*)sampleHolder.audioFile)->unplayable) {

			guide.sequenceSyncStartedAtTick =
			    playbackHandler.lastSwungTickActioned; // Must do this even if we're going to return due to time
			                                           // stretching being active

			// Obviously if no voiceSample yet, need to reset envelope for first usage. Otherwise,
			// If the envelope's now releasing (e.g. from playback just being stopped and then restarted, e.g. by
			// pressing <> + play), and if not doing a late start (which would cause more sound to start happening after
			// the envelope finishes releasing), then we'd absolutely better reset that envelope because there's no
			// other way any sound's going to come out!
			bool shouldResetEnvelope = !voiceSample
			                           || (((AudioOutput*)output)->envelope.state >= EnvelopeStage::RELEASE
			                               && !doingLateStart); // Come to think of it, would doingLateStart ever (or
			                                                    // normally) be true if we're at play pos 0?

			// If already had a VoiceSample, everything's probably all fine...
			if (voiceSample) {

				// But, if it's reading from (or writing to) a cache, we have to manually cut it out right now and tell
				// it to restart, so the new play-through will remain perfectly synced up to this start-tick we're at
				// now.
				if (voiceSample->cache

				    // Or, if no time-stretcher, which also means we're not "fudging" - probably because there was no
				    // pre-margin - then yup we'll wanna force restart now too - because AudioClip VoiceSamples don't
				    // obey their loop points, so it won't have happened otherwise.
				    || !voiceSample->timeStretcher

				    // Or, if time stretching is on but the "newer" play-head is no longer active - because it's shot
				    // past the end of the waveform, and this waveform didn't have an extra "margin" at the end, then we
				    // want to just cut everything and start from scratch again.
				    || !voiceSample->timeStretcher->playHeadStillActive[PLAY_HEAD_NEWER]) {

					// Yup, do that unassignment
doUnassignment:
					voiceSample->beenUnassigned(false);
				}

				// Or if none of those several conditions were met...
				else {
					// If here, we know time stretching is on.

					// If no pre-margin, then still do go and do the unassignment and start afresh - cos we'll wanna
					// hear that sharp start-point perfectly rather than just fading into it after a play-head reaches
					// its end
					uint32_t waveformStartByte = ((Sample*)sampleHolder.audioFile)->audioDataStartPosBytes;
					if (sampleControls.isCurrentlyReversed()) {
						waveformStartByte +=
						    ((Sample*)sampleHolder.audioFile)->audioDataLengthBytes
						    - sampleHolder.audioFile->numChannels
						          * ((Sample*)sampleHolder.audioFile)
						                ->byteDepth; // The actual first sample of the waveform in our given direction,
						                             // regardless of our elected start-point
					}
					int32_t numBytesOfPreMarginAvailable =
					    (int32_t)(guide.getBytePosToStartPlayback(true) - waveformStartByte);
					if (sampleControls.isCurrentlyReversed()) {
						numBytesOfPreMarginAvailable = -numBytesOfPreMarginAvailable;
					}
					if (numBytesOfPreMarginAvailable <= 0) {
						goto doUnassignment;
					}

					// If we were "fudging" a time-stretch just to get a free crossfade, then we can now stop doing all
					// of that. (It should automatically stop eventually anyway, but let's be clean and efficient and
					// just kill it now.)
					if (voiceSample->fudging) {
						voiceSample->endTimeStretching();
					}
					// Otherwise, if we're just regular time-stretching (not for mere "fudging" reasons), don't do
					// anything and just get out. The regular time-stretching algorithm takes care of causing playback
					// to jump back to the start of the Sample.

					goto possiblyResetEnvelopeAndGetOut;
				}
			}

			// Otherwise, get a new VoiceSample
			else {
				voiceSample = AudioEngine::solicitVoiceSample();
				if (!voiceSample) {
					return;
				}
			}

			// Ok, get playback all set up
			doingLateStart = false;
			maySetupCache = true;

			setupPlaybackBounds();

			voiceSample->noteOn(&guide, 0, 1);
			voiceSample->forAudioClip = true;
			voiceSample->setupClusersForInitialPlay(&guide, ((Sample*)sampleHolder.audioFile), 0, false, 1);

possiblyResetEnvelopeAndGetOut:
			if (shouldResetEnvelope) {
				((AudioOutput*)output)->resetEnvelope();
			}
		}
	}
}

// This must only be called if playback is on and this Clip is active!
void AudioClip::resumePlayback(ModelStackWithTimelineCounter* modelStack, bool mayMakeSound) {

#if ALPHA_OR_BETA_VERSION
	if (!playbackHandler.isEitherClockActive() || !modelStack->song->isClipActive(this)) {
		FREEZE_WITH_ERROR("E430");
	}
#endif

	if (!sampleHolder.audioFile || ((Sample*)sampleHolder.audioFile)->unplayable) {
		return;
	}

	// If reading or writing cache, that's not gonna be valid now that we've moved our play position, so gotta stop
	// that.
	if (voiceSample && voiceSample->cache) {
		int32_t priorityRating = 1;
		bool success = voiceSample->stopUsingCache(&guide, ((Sample*)sampleHolder.audioFile), priorityRating,
		                                           getLoopingType(modelStack) == LoopType::LOW_LEVEL);
		if (!success) {
			unassignVoiceSample(false);
		}
	}

	// For synced time-stretching, that syncing is done by noting what the internal tick count "was" at the start of the
	// Clip, so we work that out here. However, due to the efficient ticking system and the fact that the clip length
	// may have just been reduced, it's possible that this would then give us a result which would indicate that our
	// current play position is actually beyond the length of the Clip. So, we do some wrapping here to ensure that
	// guide.sequenceSyncStartedAtTickTrivialValue is not longer ago (from the "actual" current internal ticket time)
	// than the length of the Clip. Despite the fact that we generally here deal with the positions / counts of the last
	// swung tick, which is why this problem exists in the first place. To be clear, where wrapping occurs,
	// guide.sequenceSyncStartedAtTickTrivialValue will be after the last actioned swung tick.
	int32_t sequenceSyncStartedAtTickTrivialValue = playbackHandler.lastSwungTickActioned - lastProcessedPos;
	int32_t currentInternalTickCount = playbackHandler.getCurrentInternalTickCount();
	int32_t sequenceSyncStartedNumTicksAgo = currentInternalTickCount - sequenceSyncStartedAtTickTrivialValue;
	if (sequenceSyncStartedNumTicksAgo < 0) { // Shouldn't happen
		if (ALPHA_OR_BETA_VERSION) {
			FREEZE_WITH_ERROR("nofg"); // Ron got, Nov 2021. Wait no, he didn't have playback on!
		}
		sequenceSyncStartedNumTicksAgo = 0; // The show must go on
	}
	sequenceSyncStartedNumTicksAgo = (uint32_t)sequenceSyncStartedNumTicksAgo % (uint32_t)loopLength; // Wrapping
	guide.sequenceSyncStartedAtTick =
	    currentInternalTickCount - sequenceSyncStartedNumTicksAgo; // Finally, we've got our value

	setupPlaybackBounds();
	sampleZoneChanged(modelStack); // Will only do any thing if there is in fact a voiceSample - which is what we want

	if (!mayMakeSound) {
		return;
	}

	// If already time stretching, no need to do anything - that'll take care of the new play-position.
	// Also can only do this if envelope not releasing, which (possibly not anymore since I fixed other bug) it can
	// still be if this is our second quick successive <>+play starting playback partway through Clip
	if (voiceSample && voiceSample->timeStretcher && ((AudioOutput*)output)->envelope.state < EnvelopeStage::RELEASE) {
		return;
	}

	// Ok, get playback all set up
	doingLateStart = true;
	maySetupCache = false;

	// If already had a VoiceSample, we can reuse it
	if (voiceSample) {

		// But we're gonna do a nice quick fade-out first.
		// TODO: probably not super necessary now we've got time-stretching taking care of sorta doing a crossfade,
		// above. We'd only actually need to do any such fade manually if we weren't time-stretching before, and we're
		// also not going to be after (though it'd be hard to predict whether we're going to be after)
		((AudioOutput*)output)->envelope.unconditionalRelease(EnvelopeStage::FAST_RELEASE);
	}

	// Otherwise, get a new VoiceSample
	else {
		voiceSample = AudioEngine::solicitVoiceSample();
		if (!voiceSample) {
			return;
		}

		voiceSample->noteOn(&guide, 0, 1);
		voiceSample->forAudioClip = true;
		((AudioOutput*)output)->resetEnvelope();
	}
}

void AudioClip::setupPlaybackBounds() {
	if (sampleHolder.audioFile) {
		int32_t length = getCurrentlyRecordingLinearly() ? originalLength : loopLength;
		guide.sequenceSyncLengthTicks = length;
		guide.setupPlaybackBounds(sampleControls.isCurrentlyReversed());
	}
}

void AudioClip::sampleZoneChanged(ModelStackWithTimelineCounter const* modelStack) {
	if (voiceSample) {

		int32_t priorityRating = 1; // probably better fix this...

		voiceSample->sampleZoneChanged(&guide, ((Sample*)sampleHolder.audioFile), sampleControls.isCurrentlyReversed(),
		                               MarkerType::END, getLoopingType(modelStack), priorityRating, true);
	}
}

int64_t AudioClip::getNumSamplesTilLoop(ModelStackWithTimelineCounter* modelStack) {

	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(0, nullptr);

	int32_t cutPos = modelStackWithNoteRow->getPosAtWhichPlaybackWillCut();
	int32_t loopPosWithinClip = std::min(cutPos, loopLength);

	int32_t ticksTilLoop = loopPosWithinClip - lastProcessedPos;
	uint32_t loopTime = playbackHandler.getInternalTickTime(playbackHandler.lastSwungTickActioned + ticksTilLoop);
	return loopTime - AudioEngine::audioSampleTimer;
}

void AudioClip::render(ModelStackWithTimelineCounter* modelStack, std::span<q31_t> outputBuffer, int32_t amplitude,
                       int32_t amplitudeIncrement, int32_t pitchAdjust) {

	if (!voiceSample) {
		return;
	}

	Sample* sample = ((Sample*)sampleHolder.audioFile);

	// First, if we're still attempting to do a "late start", see if we can do that (perhaps not if relevant audio data
	// hasn't loaded yet)
	if (doingLateStart && static_cast<AudioOutput*>(this->output)->envelope.state < EnvelopeStage::FAST_RELEASE) {
		uint64_t numSamplesIn = guide.getSyncedNumSamplesIn();

		LateStartAttemptStatus result = voiceSample->attemptLateSampleStart(&guide, sample, numSamplesIn);
		if (result != LateStartAttemptStatus::SUCCESS) {
			if (result == LateStartAttemptStatus::FAILURE) {
				unassignVoiceSample(false);
			}
			return;
		}

		doingLateStart = false;
	}

	int32_t timeStretchRatio = kMaxSampleValue;
	int32_t phaseIncrement = sampleHolder.neutralPhaseIncrement;

	if (pitchAdjust != kMaxSampleValue) {
		uint64_t newPhaseIncrement = ((uint64_t)phaseIncrement * (uint64_t)pitchAdjust) >> 24;
		phaseIncrement = (newPhaseIncrement > 2147483647) ? 2147483647 : newPhaseIncrement;
	}

	uint32_t sampleLengthInSamples = sampleHolder.getDurationInSamples(true); // In the sample rate of the file!
	uint32_t clipLengthInSamples =
	    (playbackHandler.getTimePerInternalTickBig() * loopLength) >> 32; // We haven't rounded... should we?

	// To stop things getting insane, limit to 32x speed
	// if ((sampleLengthInSamples >> 5) > clipLengthInSamples) return false;

	uint64_t requiredSpeedAdjustment =
	    (uint64_t)(((double)((uint64_t)sampleLengthInSamples << 24)) / (double)clipLengthInSamples);

	// If we're squishing time...
	if (sampleControls.pitchAndSpeedAreIndependent) {
		timeStretchRatio = requiredSpeedAdjustment;

		// And if pitch was manually adjusted too (or file's sample rate wasn't 44100kHz, that's fine - counteract that
		// by adjusting the time-stretch amount more
		if (phaseIncrement != kMaxSampleValue) {
			timeStretchRatio = ((uint64_t)timeStretchRatio << 24) / (uint32_t)phaseIncrement;
		}

		// Or if no manual pitch adjustment...
		else {

			// If we'd only be time-stretching a tiiiny bit (+/- 1 cent)...
			if (timeStretchRatio >= 16661327 && timeStretchRatio < 16893911) {

				// And if playback has stopped or the envelope is doing a fast release before we begin another "late
				// start"...
				if (!playbackHandler.isEitherClockActive() || doingLateStart) {
justDontTimeStretch:
					// We can just not time-stretch... for now, until we end up more out-of-sync later
					timeStretchRatio = kMaxSampleValue;
				}

				// Or...
				else {

					// If we're less than 7.8mS out of sync, then that's another (even more common) reason not to
					// time-stretch
					int32_t numSamplesLaggingBehindSync = guide.getNumSamplesLaggingBehindSync(voiceSample);
					int32_t numSamplesDrift = std::abs(numSamplesLaggingBehindSync);

					if (numSamplesDrift < (sample->sampleRate >> 7)) {
						goto justDontTimeStretch;
					}
					else {
						D_PRINTLN("sync:  %d", numSamplesDrift);
					}
				}
			}
		}
	}

	// Or if we're squishing pitch...
	else {

		// If no prior pitch adjustment, we play back 100% natively, with no pitch shifting / time stretching. Just with
		// pitch / speed changed like speeding/slowing a record
		if (!sampleHolder.transpose && !sampleHolder.cents) {
			uint64_t phaseIncrementNew = ((uint64_t)(uint32_t)phaseIncrement * requiredSpeedAdjustment) >> 24;
			phaseIncrement = (phaseIncrementNew >= 2147483648) ? 2147483647 : (uint32_t)phaseIncrementNew;

			if (playbackHandler.isEitherClockActive() && !doingLateStart) {
				phaseIncrement = guide.adjustPitchToCorrectDriftFromSync(voiceSample, phaseIncrement);
			}
		}

		// Or if yes prior pitch adjustment, then we'll be pitch shifting / time stretching
		else {
			timeStretchRatio = ((uint64_t)1 << 48) / (uint32_t)phaseIncrement;
			phaseIncrement = ((uint64_t)phaseIncrement * requiredSpeedAdjustment) >> 24;
		}
	}

	int32_t priorityRating = 1;

	bool clipWillLoopAtEnd = playbackHandler.playbackState && currentPlaybackMode->willClipLoopAtSomePoint(modelStack);
	// I don't love that this is being called for every AudioClip at every render. This is to determine the "looping"
	// parameter for the call to VoiceSample::render() below. It'd be better to maybe have that just query the
	// currentPlaybackMode when it needs to actually use this info, cos it only occasionally needs it

	bool stillActive;

	// If Clip will loop at end...
	if (clipWillLoopAtEnd) {

		// If no time-stretcher, and not reading cache, we might want to "fudge" to eliminate the click at the loop
		// point (if it's gonna loop)
		if (timeStretchRatio == kMaxSampleValue && !voiceSample->timeStretcher
		    && (!voiceSample->cache || voiceSample->writingToCache)) {

			// First, see if there is actually any pre-margin at all for us to crossfade to

			int32_t bytesPerSample = sample->byteDepth * sample->numChannels;

			int32_t startByte = sample->audioDataStartPosBytes;
			if (guide.playDirection != 1) {
				startByte += sample->audioDataLengthBytes
				             - bytesPerSample; // The actual first sample of the waveform in our given direction,
				                               // regardless of our elected start-point
			}

			int32_t numBytesOfPreMarginAvailable =
			    (int32_t)(guide.getBytePosToStartPlayback(true) - startByte) * guide.playDirection;

			if (numBytesOfPreMarginAvailable > 0) {

				int64_t numSamplesTilLoop = getNumSamplesTilLoop(modelStack);

				if (numSamplesTilLoop <= kAntiClickCrossfadeLength) {

					int32_t numSamplesOfPreMarginAvailable =
					    (uint32_t)numBytesOfPreMarginAvailable / (uint8_t)bytesPerSample;
					if (phaseIncrement != kMaxSampleValue) {
						numSamplesOfPreMarginAvailable =
						    ((uint64_t)numSamplesOfPreMarginAvailable << 24) / (uint32_t)phaseIncrement;
					}

					if (numSamplesOfPreMarginAvailable > 2) {
						// D_PRINTLN("");
						// D_PRINTLN("might attempt fudge");

						int32_t crossfadeLength = std::min(numSamplesOfPreMarginAvailable, kAntiClickCrossfadeLength);

						// If we're right at the end and it's time to crossfade...
						if (numSamplesTilLoop <= crossfadeLength) {

							// Fudge some time-stretching
							bool success = voiceSample->fudgeTimeStretchingToAvoidClick(
							    sample, &guide, phaseIncrement, numSamplesTilLoop, guide.playDirection, priorityRating);
							if (!success) {
								goto doUnassign;
							}
						}
					}
				}
			}
		}
	}

	// Or if Clip won't loop at any point...
	else {

		// We want to do a fast release *before* the end, to finish right as the end is reached. So that any waveform
		// after the end isn't heard.
		// TODO: in an ideal world, would we only do this if there actually is some waveform "margin" after the end that
		// we want to avoid hearing, and otherwise just do the release right at the end (does that already happen, I
		// forgot?) It's perhaps a little bit surprising, but this even works and sounds perfect (you never hear any of
		// the margin) when time-stretching is happening! Down to about half speed. Below that, you hear some of the
		// margin.
		if (static_cast<AudioOutput*>(this->output)->envelope.state < EnvelopeStage::FAST_RELEASE) {

			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(0, nullptr);

			int32_t cutPos = modelStackWithNoteRow->getPosAtWhichPlaybackWillCut();
			if (cutPos < 2147483647) {

				int32_t ticksTilCut = cutPos - lastProcessedPos;
				uint32_t loopTime =
				    playbackHandler.getInternalTickTime(playbackHandler.lastSwungTickActioned + ticksTilCut);
				int32_t timeTilLoop = loopTime - AudioEngine::audioSampleTimer;

				if (timeTilLoop < 1024) {
					static_cast<AudioOutput*>(this->output)
					    ->envelope.unconditionalRelease(EnvelopeStage::FAST_RELEASE, 8192); // Let's make it extra fast?
				}
			}
		}
	}

	if (maySetupCache) {
		maySetupCache = false;
		// We tell the cache setup that we're *not* looping.
		// We want it to think this, otherwise problems if we've put the end point after the actual waveform end.
		bool everythingOk = voiceSample->possiblySetUpCache(&sampleControls, &guide, phaseIncrement, timeStretchRatio,
		                                                    1, LoopType::NONE);
		if (!everythingOk) {
			goto doUnassign;
		}
	}

	{
		LoopType loopingType = getLoopingType(modelStack);

		stillActive = voiceSample->render(&guide, outputBuffer.data(), outputBuffer.size(), sample, sample->numChannels,
		                                  loopingType, phaseIncrement, timeStretchRatio, amplitude, amplitudeIncrement,
		                                  sampleControls.getInterpolationBufferSize(phaseIncrement),
		                                  sampleControls.interpolationMode, 1);
	}

	if (!stillActive) {
doUnassign:
		unassignVoiceSample(false);
	}
}

// Returns the "looping" parameter that gets passed into a lot of functions.
LoopType AudioClip::getLoopingType(ModelStackWithTimelineCounter const* modelStack) {

	// We won't loop at the low level. We may want to loop at time-stretcher level, in the following case (not if the
	// end-point is set to beyond the waveform's length).

	bool shouldLoop = (sampleControls.isCurrentlyReversed()
	                   || sampleHolder.endPos <= ((Sample*)sampleHolder.audioFile)->lengthInSamples)
	                  && currentPlaybackMode->willClipContinuePlayingAtEnd(modelStack);

	return shouldLoop ? LoopType::TIMESTRETCHER_LEVEL_IF_ACTIVE : LoopType::NONE;

	// ---- This is the old comment / logic we previously had, here
	// Normally we'll loop at the lowest level - but not if user has inserted silence at the end
	// (put the end-pos beyond the end of the Sample)

	// return (sampleControls.isCurrentlyReversed() || sampleHolder.endPos <=
	// ((Sample*)sampleHolder.audioFile)->lengthInSamples);

	// Note that the actual "loop points" don't get obeyed for AudioClips - if any looping happens at the low level,
	// it'll only be at the very end of the waveform if we happen to reach it.
	// But, looping may still happen as normally expected at the TimeStretcher level...
}

void AudioClip::unassignVoiceSample(bool wontBeUsedAgain) {
	if (voiceSample) {
		voiceSample->beenUnassigned(wontBeUsedAgain);
		AudioEngine::voiceSampleUnassigned(voiceSample);
		voiceSample = nullptr;
	}
}

void AudioClip::expectNoFurtherTicks(Song* song, bool actuallySoundChange) {

	// If it's actually another Clip, that we're recording into the arranger...
	if (output->getActiveClip() && output->getActiveClip()->beingRecordedFromClip == this) {
		output->getActiveClip()->expectNoFurtherTicks(song, actuallySoundChange);
		return;
	}

	if (voiceSample) {
		if (actuallySoundChange) {

			// Fix only added for bug / crash discovered in Feb 2021!
			if (doingLateStart) {
				// If waiting to do a late start, and we're not waiting for a past bit to fade out, well there's no
				// sound right now, so just cut out.
				if (((AudioOutput*)output)->envelope.state < EnvelopeStage::FAST_RELEASE) {
					unassignVoiceSample(false);
				}

				// Or if we were planning to do a late start as soon as the current sound fades out, then just abandon
				// the late start, but keep doing the fade
				else {
					doingLateStart = false;
				}
			}
			else {
				// Or normal case - do a fade when we weren't going to before. And no late start is or was happening.
				((AudioOutput*)output)->envelope.unconditionalRelease(EnvelopeStage::FAST_RELEASE);
			}
		}
	}

	if (recorder) {
		abortRecording();
	}
}

// May change the TimelineCounter in the modelStack if new Clip got created
void AudioClip::posReachedEnd(ModelStackWithTimelineCounter* modelStack) {

	if (!isEmpty()) {}

	Clip::posReachedEnd(modelStack);

	// If recording from session to arranger...
	if (playbackHandler.recording == RecordingMode::ARRANGEMENT && isArrangementOnlyClip()) {

		D_PRINTLN("");
		D_PRINTLN("AudioClip::posReachedEnd, at pos:  %d", playbackHandler.getActualArrangementRecordPos());

		if (!modelStack->song->arrangementOnlyClips.ensureEnoughSpaceAllocated(1)) {
			return;
		}
		if (!output->clipInstances.ensureEnoughSpaceAllocated(1)) {
			return;
		}

		int32_t arrangementRecordPos = playbackHandler.getActualArrangementRecordPos();

		// Get that current clipInstance being recorded to
		int32_t clipInstanceI = output->clipInstances.search(arrangementRecordPos, LESS);
		if (clipInstanceI >= 0) {
			ClipInstance* clipInstance = output->clipInstances.getElement(clipInstanceI);

			// Close it off
			clipInstance->length = arrangementRecordPos - clipInstance->pos;
		}

		Error error = beingRecordedFromClip->clone(modelStack); // Puts the new Clip in the modelStack.
		if (error != Error::NONE) {
			return;
		}

		Clip* newClip = (Clip*)modelStack->getTimelineCounter();

		newClip->beingRecordedFromClip = beingRecordedFromClip;
		beingRecordedFromClip = nullptr;

		newClip->section = 255;

		modelStack->song->arrangementOnlyClips.insertClipAtIndex(newClip, 0); // Can't fail - checked above

		clipInstanceI++;

		error = output->clipInstances.insertAtIndex(clipInstanceI); // Shouldn't be able to fail...
		if (error != Error::NONE) {
			return;
		}

		ClipInstance* clipInstance = output->clipInstances.getElement(clipInstanceI);
		clipInstance->clip = newClip;
		clipInstance->pos = arrangementRecordPos;
		clipInstance->length = loopLength;

		newClip->activeIfNoSolo = false; // And now, we want it to actually be false
		output->setActiveClip(modelStack, PgmChangeSend::NEVER);

		newClip->setPos(modelStack, 0, false); // Tell it to *not* use "live pos"

		newClip->paramManager.getUnpatchedParamSet()->copyOverridingFrom(paramManager.getUnpatchedParamSet());
	}
}

// Can assume there always was an old Output to begin with
// Does not dispose of the old Output - the caller has to do this
void AudioClip::detachFromOutput(ModelStackWithTimelineCounter* modelStack, bool shouldRememberDrumNames,
                                 bool shouldDeleteEmptyNoteRowsAtEitherEnd, bool shouldRetainLinksToOutput,
                                 bool keepNoteRowsWithMIDIInput, bool shouldGrabMidiCommands,
                                 bool shouldBackUpExpressionParamsToo) {
	detachAudioClipFromOutput(modelStack->song, shouldRetainLinksToOutput);
}

void AudioClip::detachAudioClipFromOutput(Song* song, bool shouldRetainLinksToOutput, bool shouldTakeParamManagerWith) {
	// detaching from output, so don't need it anymore
	unassignVoiceSample(true);

	if (isActiveOnOutput()) {
		output->detachActiveClip(song);
	}

	// Normal case, where we're leaving ParamManager behind with old Output
	if (!shouldTakeParamManagerWith) {
doNormal:
		song->backUpParamManager((ModControllableAudio*)output->toModControllable(), this, &paramManager);
	}

	// Or, special case where we're keeping the ParamManager
	else {

		// If our ParamManager was the only one that the old Output had, we have to clone it
		if (!song->getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)output->toModControllable(), this)
		    && !song->getClipWithOutput(output, false, this)) {

			ParamManagerForTimeline newParamManager;
			Error error = newParamManager.cloneParamCollectionsFrom(&paramManager, true);
			if (error != Error::NONE) {
				goto doNormal; // If out of RAM, leave ParamManager behind
			}

			song->backUpParamManager((ModControllableAudio*)output->toModControllable(), nullptr, &newParamManager);
			// Obscure bug fixed, Oct 2022. Previously, we filed it away under not just the ModControllable, but also
			// this Clip. Perhaps that's still theoretically what we'd like to do? But, it caused an E170 because when
			// we do a song swap and call Song::deleteSoundsWhichWontSound(), that calls
			// deleteAllBackedUpParamManagersWithClips(), which would then delete this ParamManager here, because we
			// gave it a Clip. But it's actually still needed by the associated ModControllable / AudioOutput.
		}
	}

	if (!shouldRetainLinksToOutput) {
		output = nullptr;
	}
}

int64_t AudioClip::getSamplesFromTicks(int32_t ticks) {
	if (recorder) {
		return playbackHandler.getTimePerInternalTickFloat() * ticks;
	}
	else {
		int32_t length = getCurrentlyRecordingLinearly() ? originalLength : loopLength;
		return (int64_t)ticks * sampleHolder.getDurationInSamples(true)
		       / length; // Yup, ticks could be negative, and so could the result
	}
}

// Only call this if you know there's a Sample
void AudioClip::getScrollAndZoomInSamples(int32_t xScroll, int32_t xZoom, int64_t* xScrollSamples,
                                          int64_t* xZoomSamples) {

	// Tempoless or arranger recording
	if (recorder && (!playbackHandler.isEitherClockActive() || currentPlaybackMode == &arrangement)) {
		*xScrollSamples = recorder->sample->fileLoopStartSamples;
		int32_t numSamplesCapturedPastLoopStart = recorder->numSamplesCaptured - *xScrollSamples;
		*xZoomSamples = (numSamplesCapturedPastLoopStart < kDisplayWidth)
		                    ? 1
		                    : numSamplesCapturedPastLoopStart >> kDisplayWidthMagnitude;
	}

	// Or, normal...
	else {
		*xZoomSamples = getSamplesFromTicks(xZoom);

		int64_t xScrollSamplesWithinZone = getSamplesFromTicks(xScroll);

		if (sampleControls.isCurrentlyReversed()) {
			*xScrollSamples =
			    sampleHolder.getEndPos(true) - xScrollSamplesWithinZone - (*xZoomSamples << kDisplayWidthMagnitude);
		}
		else {
			int64_t sampleStartPos = recorder ? recorder->sample->fileLoopStartSamples : sampleHolder.startPos;
			*xScrollSamples = xScrollSamplesWithinZone + sampleStartPos;
		}
	}
}

// Returns false if can't because in card routine
bool AudioClip::renderAsSingleRow(ModelStackWithTimelineCounter* modelStack, TimelineView* editorScreen,
                                  int32_t xScroll, uint32_t xZoom, RGB* image, uint8_t occupancyMask[],
                                  bool addUndefinedArea, int32_t noteRowIndexStart, int32_t noteRowIndexEnd,
                                  int32_t xStart, int32_t xEnd, bool allowBlur, bool drawRepeats) {

	// D_PRINTLN("AudioClip::renderAsSingleRow");

	Sample* sample;
	if (recorder) {
		sample = ((recorder->status == RecorderStatus::ABORTED || recorder->reachedMaxFileSize) ? nullptr
		                                                                                        : recorder->sample);
	}
	else {
		sample = ((Sample*)sampleHolder.audioFile);
	}

	if (sample) {

		int64_t xScrollSamples;
		int64_t xZoomSamples;

		getScrollAndZoomInSamples(xScroll, xZoom, &xScrollSamples, &xZoomSamples);

		RGB rgb = getColour();

		bool success =
		    waveformRenderer.renderAsSingleRow(sample, xScrollSamples, xZoomSamples, image, &renderData, recorder, rgb,
		                                       sampleControls.isCurrentlyReversed(), xStart, xEnd);

		if (!success) {
			// If card being accessed and waveform would have to be re-examined, come back later
			return false;
		}
	}

	else {
		Clip::renderAsSingleRow(modelStack, editorScreen, xScroll, xZoom, image, occupancyMask, addUndefinedArea,
		                        noteRowIndexStart, noteRowIndexEnd, xStart, xEnd, allowBlur, drawRepeats);
	}

	if (addUndefinedArea) {
		drawUndefinedArea(xScroll, xZoom, loopLength, image, occupancyMask, kDisplayWidth, editorScreen, false);
	}

	return true;
}

void AudioClip::writeDataToFile(Serializer& writer, Song* song) {

	writer.writeAttribute("trackName", output->name.get());

	writer.writeAttribute("filePath", sampleHolder.audioFile ? sampleHolder.audioFile->filePath.get()
	                                                         : sampleHolder.filePath.get());
	writer.writeAttribute("startSamplePos", sampleHolder.startPos);
	writer.writeAttribute("endSamplePos", sampleHolder.endPos);
	writer.writeAttribute("pitchSpeedIndependent", sampleControls.pitchAndSpeedAreIndependent);
	if (sampleControls.interpolationMode == InterpolationMode::LINEAR) {
		writer.writeAttribute("linearInterpolation", 1);
	}
	if (sampleControls.isCurrentlyReversed()) {
		writer.writeAttribute("reversed", 1);
	}
	writer.writeAttribute("attack", attack);
	writer.writeAttribute("priority", util::to_underlying(voicePriority));

	if (sampleHolder.transpose) {
		writer.writeAttribute("transpose", sampleHolder.transpose);
	}
	if (sampleHolder.cents) {
		writer.writeAttribute("cents", sampleHolder.cents);
	}

	writer.writeAttribute("overdubsShouldCloneAudioTrack", overdubsShouldCloneOutput);

	if (onAutomationClipView) {
		writer.writeAttribute("onAutomationInstrumentClipView", 1);
	}
	if (lastSelectedParamID != kNoSelection) {
		writer.writeAttribute("lastSelectedParamID", lastSelectedParamID);
		writer.writeAttribute("lastSelectedParamKind", util::to_underlying(lastSelectedParamKind));
		writer.writeAttribute("lastSelectedParamShortcutX", lastSelectedParamShortcutX);
		writer.writeAttribute("lastSelectedParamShortcutY", lastSelectedParamShortcutY);
		writer.writeAttribute("lastSelectedParamArrayPosition", lastSelectedParamArrayPosition);
	}

	Clip::writeDataToFile(writer, song);
	Clip::writeDataToFile(writer, song);

	writer.writeOpeningTagEnd();

	Clip::writeMidiCommandsToFile(writer, song);

	writer.writeOpeningTagBeginning("params");
	GlobalEffectableForClip::writeParamAttributesToFile(writer, &paramManager, true);
	writer.writeOpeningTagEnd();
	GlobalEffectableForClip::writeParamTagsToFile(writer, &paramManager, true);
	writer.writeClosingTag("params");
}

Error AudioClip::readFromFile(Deserializer& reader, Song* song) {

	Error error;

	if (false) {
ramError:
		error = Error::INSUFFICIENT_RAM;

someError:
		return error;
	}

	char const* tagName;

	int32_t readAutomationUpToPos = kMaxSequenceLength;

	while (*(tagName = reader.readNextTagOrAttributeName())) {
		// D_PRINTLN(tagName); delayMS(30);

		if (!strcmp(tagName, "trackName")) {
			reader.readTagOrAttributeValueString(&outputNameWhileLoading);
		}

		else if (!strcmp(tagName, "filePath")) {
			reader.readTagOrAttributeValueString(&sampleHolder.filePath);
		}

		else if (!strcmp(tagName, "overdubsShouldCloneAudioTrack")) {
			overdubsShouldCloneOutput = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "startSamplePos")) {
			sampleHolder.startPos = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "endSamplePos")) {
			sampleHolder.endPos = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "pitchSpeedIndependent")) {
			sampleControls.pitchAndSpeedAreIndependent = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "linearInterpolation")) {
			if (reader.readTagOrAttributeValueInt()) {
				sampleControls.interpolationMode = InterpolationMode::LINEAR;
			}
		}

		else if (!strcmp(tagName, "attack")) {
			attack = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "priority")) {
			voicePriority = static_cast<VoicePriority>(reader.readTagOrAttributeValueInt());
		}

		else if (!strcmp(tagName, "reversed")) {
			sampleControls.reversed = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "transpose")) {
			sampleHolder.transpose = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "cents")) {
			sampleHolder.cents = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "params")) {
			paramManager.setupUnpatched();
			GlobalEffectableForClip::initParams(&paramManager);
			GlobalEffectableForClip::readParamsFromFile(reader, &paramManager, readAutomationUpToPos);
		}

		else if (!strcmp(tagName, "onAutomationInstrumentClipView")) {
			onAutomationClipView = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "lastSelectedParamID")) {
			lastSelectedParamID = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "lastSelectedParamKind")) {
			lastSelectedParamKind = static_cast<params::Kind>(reader.readTagOrAttributeValueInt());
		}

		else if (!strcmp(tagName, "lastSelectedParamShortcutX")) {
			lastSelectedParamShortcutX = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "lastSelectedParamShortcutY")) {
			lastSelectedParamShortcutY = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "lastSelectedParamArrayPosition")) {
			lastSelectedParamArrayPosition = reader.readTagOrAttributeValueInt();
		}

		else {
			readTagFromFile(reader, tagName, song, &readAutomationUpToPos);
		}

		reader.exitTag();
	}

	return Error::NONE;
}

Error AudioClip::claimOutput(ModelStackWithTimelineCounter* modelStack) {

	output = modelStack->song->getAudioOutputFromName(&outputNameWhileLoading);

	if (!output) {
		return Error::FILE_CORRUPTED;
	}

	return Error::NONE;
}

void AudioClip::loadSample(bool mayActuallyReadFile) {
	Error error = sampleHolder.loadFile(sampleControls.isCurrentlyReversed(), false, mayActuallyReadFile);
	name.set(sampleHolder.filePath.get());
	if (error != Error::NONE) {
		display->displayError(error);
	}
}

// Keeps same ParamManager
Error AudioClip::changeOutput(ModelStackWithTimelineCounter* modelStack, Output* newOutput) {
	detachAudioClipFromOutput(modelStack->song, false, true);

	return setOutput(modelStack, newOutput);
}

Error AudioClip::setOutput(ModelStackWithTimelineCounter* modelStack, Output* newOutput,
                           AudioClip* favourClipForCloningParamManager) {
	output = newOutput;
	Error error = solicitParamManager(modelStack->song, nullptr, favourClipForCloningParamManager);
	if (error != Error::NONE) {
		return error;
	}

	outputChanged(modelStack, newOutput);

	return Error::NONE;
}

RGB AudioClip::getColour() {
	return RGB::fromHuePastel(colourOffset * -8 / 3);
}

void AudioClip::quantizeLengthForArrangementRecording(ModelStackWithTimelineCounter* modelStack,
                                                      int32_t lengthSoFarInternalTicks, uint32_t timeRemainder,
                                                      int32_t suggestedLength, int32_t alternativeLongerLength) {

	double numTicksDone =
	    (double)lengthSoFarInternalTicks + (double)timeRemainder / playbackHandler.getTimePerInternalTickFloat();

	double samplesPerTick = (double)sampleHolder.getDurationInSamples(true) / numTicksDone;

	sampleHolder.endPos = sampleHolder.startPos + samplesPerTick * suggestedLength + 0.5; // Rounds it

	int32_t oldLength = loopLength;
	loopLength = suggestedLength;
	originalLength = loopLength;
	lengthChanged(modelStack, oldLength, nullptr);
}

bool AudioClip::currentlyScrollableAndZoomable() {
	bool shouldLock = (getCurrentlyRecordingLinearly()
	                   && (!playbackHandler.isEitherClockActive() || currentPlaybackMode == &arrangement));
	return !shouldLock;
}

void AudioClip::clear(Action* action, ModelStackWithTimelineCounter* modelStack, bool clearAutomation,
                      bool clearSequenceAndMPE) {
	Clip::clear(action, modelStack, clearAutomation, clearSequenceAndMPE);

	// if clearSequenceAndMPE is true, clear sample
	if (clearSequenceAndMPE) {
		// If recording, stop that - but only if we're not doing tempoless recording
		if (recorder) {
			if (!playbackHandler.isEitherClockActive()) {
				abortRecording();
			}
		}
		// with overdubs these could both be true
		if (sampleHolder.audioFile) {
			// we're not actually deleting the song, but we don't want to keep this sample cached since we can't get it
			// back anyway
			unassignVoiceSample(true);

			if (action) {
				// Must happen first
				action->recordAudioClipSampleChange(this);
			}

			sampleHolder.filePath.clear();
			sampleHolder.setAudioFile(nullptr);
			name.set("");
		}

		renderData.xScroll = -1;
	}
}

bool AudioClip::getCurrentlyRecordingLinearly() {
	return recorder;
}

void AudioClip::setPos(ModelStackWithTimelineCounter* modelStack, int32_t newPos, bool useActualPosForParamManagers) {

	Clip::setPos(modelStack, newPos, useActualPosForParamManagers);

	setPosForParamManagers(modelStack, useActualPosForParamManagers);
}

bool AudioClip::shiftHorizontally(ModelStackWithTimelineCounter* modelStack, int32_t amount, bool shiftAutomation,
                                  bool shiftSequenceAndMPE) {
	// the following code iterates through all param collections and shifts automation and MPE separately
	// automation only gets shifted if shiftAutomation is true
	// MPE only gets shifted if shiftSequenceAndMPE is true
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);

	if (paramManager.containsAnyParamCollectionsIncludingExpression()) {
		ParamCollectionSummary* summary = paramManager.summaries;

		int32_t i = 0;

		while (summary->paramCollection) {

			ModelStackWithParamCollection* modelStackWithParamCollection =
			    modelStackWithThreeMainThings->addParamCollection(summary->paramCollection, summary);

			// Special case for MPE only - not even "mono" / Clip-level expression.
			if (i == paramManager.getExpressionParamSetOffset()) {
				if (shiftSequenceAndMPE) {
					((ExpressionParamSet*)summary->paramCollection)
					    ->shiftHorizontally(modelStackWithParamCollection, amount, loopLength);
				}
			}

			// Normal case (non MPE automation)
			else {
				if (shiftAutomation) {
					summary->paramCollection->shiftHorizontally(modelStackWithParamCollection, amount, loopLength);
				}
			}
			summary++;
			i++;
		}
	}

	// if shiftSequenceAndMPE is true, shift sample
	if (shiftSequenceAndMPE) {
		// No horizontal shift when recording
		if (recorder) {
			return false;
		}

		// No horizontal shift when no sample is loaded
		if (!sampleHolder.audioFile) {
			return false;
		}

		int64_t newStartPos = int64_t(sampleHolder.startPos) - getSamplesFromTicks(amount);
		uint64_t sampleLength = ((Sample*)sampleHolder.audioFile)->lengthInSamples;

		if (newStartPos < 0 || newStartPos > sampleLength) {
			return false;
		}

		uint64_t length = sampleHolder.endPos - sampleHolder.startPos;

		// Stop the clip if it is playing
		bool active = (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(this) && voiceSample);
		unassignVoiceSample(false);

		sampleHolder.startPos = newStartPos;
		sampleHolder.endPos = newStartPos + length;

		sampleHolder.claimClusterReasons(sampleControls.isCurrentlyReversed(), CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE);

		if (active) {
			expectEvent();
			reGetParameterAutomation(modelStack);

			// Resume the clip if it was playing before
			getCurrentClip()->resumePlayback(modelStack, true);
		}
		return true;
	}
	return false;
}

uint64_t AudioClip::getCullImmunity() {
	uint32_t distanceFromEnd = loopLength - getLivePos();
	// We're gonna cull time-stretching ones first
	bool doingTimeStretching = (voiceSample && voiceSample->timeStretcher);
	return ((uint64_t)voicePriority << 33) + ((uint64_t)!doingTimeStretching << 32) + distanceFromEnd;
}

ParamManagerForTimeline* AudioClip::getCurrentParamManager() {
	return &paramManager;
}

bool AudioClip::isEmpty(bool displayPopup) {
	if (sampleHolder.audioFile) {
		return false;
	}
	return true;
}
