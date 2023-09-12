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
#include "definitions_cxx.hpp"
#include "dsp/timestretch/time_stretcher.h"
#include "gui/ui/root_ui.h"
#include "gui/waveform/waveform_renderer.h"
#include "io/debug/print.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/clip_instance.h"
#include "model/consequence/consequence_output_existence.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "model/voice/voice_sample.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/playback_mode.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/flash_storage.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include "util/misc.h"
#include <new>

extern "C" {
#include "RZA1/uart/sio_char.h"
extern uint8_t currentlyAccessingCard;
}

AudioClip::AudioClip() : Clip(CLIP_TYPE_AUDIO) {
	overdubsShouldCloneOutput = true;
	voiceSample = NULL;
	guide.audioFileHolder = &sampleHolder; // It needs to permanently point here

	sampleControls.pitchAndSpeedAreIndependent = true;

	recorder = NULL;

	renderData.xScroll = -1;

	voicePriority = VoicePriority::MEDIUM;
	attack = -2147483648;
}

AudioClip::~AudioClip() {
	if (recorder) {
		display->freezeWithError("E278");
	}

	// Sirhc actually got this in a V3.0.5 RC! No idea how. Also Qui got around V3.1.3.
	// I suspect that recorder is somehow still set when this Clip gets "deleted" by being put in a ConsequenceClipExistence.
	// Somehow neither abortRecording() nor finishLinearRecording() would be being called when some other AudioClip becomes the Output's activeClip.
	// These get called in slightly roundabout ways, so this seems the most likely. I've added some further error code diversification.
}

// Will replace the Clip in the modelStack, if success.
int32_t AudioClip::clone(ModelStackWithTimelineCounter* modelStack, bool shouldFlattenReversing) {

	void* clipMemory = GeneralMemoryAllocator::get().alloc(sizeof(AudioClip), NULL, false, true);
	if (!clipMemory) {
		return ERROR_INSUFFICIENT_RAM;
	}

	AudioClip* newClip = new (clipMemory) AudioClip();

	newClip->copyBasicsFrom(this);
	int32_t error = newClip->paramManager.cloneParamCollectionsFrom(&paramManager, true);
	if (error) {
		newClip->~AudioClip();
		GeneralMemoryAllocator::get().dealloc(clipMemory);
		return error;
	}

	modelStack->setTimelineCounter(newClip);

	newClip->activeIfNoSolo = false;
	newClip->soloingInSessionMode = false;
	newClip->output = output;

	newClip->attack = attack;

	memcpy(&newClip->sampleControls, &sampleControls, sizeof(sampleControls));
	newClip->voicePriority = voicePriority;

	newClip->sampleHolder.beenClonedFrom(&sampleHolder, sampleControls.reversed);

	return NO_ERROR;
}

void AudioClip::copyBasicsFrom(Clip* otherClip) {
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

	recorder = NULL;

	session.justAbortedSomeLinearRecording();

	if (getRootUI()) {
		getRootUI()->clipNeedsReRendering(this);
	}

	actionLogger.notifyClipRecordingAborted(this);
}

bool AudioClip::wantsToBeginLinearRecording(Song* song) {
	return (Clip::wantsToBeginLinearRecording(song) && !sampleHolder.audioFile
	        && ((AudioOutput*)output)->inputChannel > AudioInputChannel::NONE);
}

bool AudioClip::isAbandonedOverdub() {
	return (isUnfinishedAutoOverdub && !sampleHolder.audioFile);
}

int32_t AudioClip::beginLinearRecording(ModelStackWithTimelineCounter* modelStack, int32_t buttonPressLatency) {

	AudioInputChannel inputChannel = ((AudioOutput*)output)->inputChannel;

	int32_t numChannels =
	    (inputChannel >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION || inputChannel == AudioInputChannel::STEREO) ? 2
	                                                                                                             : 1;

	bool shouldRecordMarginsNow =
	    FlashStorage::audioClipRecordMargins && inputChannel < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION;

	recorder = AudioEngine::getNewRecorder(numChannels, AudioRecordingFolder::CLIPS, inputChannel, true,
	                                       shouldRecordMarginsNow, buttonPressLatency);
	if (!recorder) {
		return ERROR_INSUFFICIENT_RAM;
	}
	recorder->autoDeleteWhenDone = true;
	return Clip::beginLinearRecording(modelStack, buttonPressLatency);
}

void AudioClip::finishLinearRecording(ModelStackWithTimelineCounter* modelStack, Clip* nextPendingOverdub,
                                      int32_t buttonLatencyForTempolessRecord) {
	if (!recorder) {
		return; // Shouldn't ever happen?
	}

	// Got to check reachedMaxFileSize here, cos that'll go true a bit before cardRoutine() sets status to ERROR
	if (recorder->status == RECORDER_STATUS_ABORTED || recorder->reachedMaxFileSize) {
		abortRecording();
		return;
	}

	// Have to do this before setting currentlyRecordingLinearly to false, for vertical scroll reasons
	Action* action = actionLogger.getNewAction(ACTION_RECORD, ACTION_ADDITION_ALLOWED);

	if (!isUnfinishedAutoOverdub) {

		if (action) {
			// Must happen before sampleHolder.filePath.set()
			action->recordAudioClipSampleChange(this);
		}
	}

	recorder->pointerHeldElsewhere = false;

	recorder->endSyncedRecording(
	    buttonLatencyForTempolessRecord); // Must call before setSample(), cos it sets up important stuff like the sample length

	// SampleRecorder will also call sampleNeedsReRendering() when "capturing" is finished, but in plenty of cases, that
	// will have happened in the above call to endSyncedRecording(), and our sample hasn't been set yet, so that won't have
	// been effective. So, we have to call this here too, to cover our bases.
	if (getRootUI()) {
		getRootUI()->clipNeedsReRendering(this);
	}

	sampleHolder.filePath.set(&recorder->sample->filePath);
	sampleHolder.setAudioFile(
	    recorder->sample, sampleControls.reversed, true,
	    CLUSTER_DONT_LOAD); // Adds a reason to the first Cluster(s). Must call this after endSyncedRecording(), which puts some final values in the Sample

	renderData.xScroll = -1; // Force re-render - though this would surely happen anyway

	if (recorder->recordingExtraMargins) {
		attack = kAudioClipDefaultAttackIfPreMargin; // TODO: make these undoable?
	}

	isUnfinishedAutoOverdub = false;

	recorder = NULL;
}

Clip* AudioClip::cloneAsNewOverdub(ModelStackWithTimelineCounter* modelStackOldClip, OverDubType newOverdubNature) {

	// Allocate memory for audio clip
	void* clipMemory = GeneralMemoryAllocator::get().alloc(sizeof(AudioClip), NULL, false, true);
	if (!clipMemory) {
ramError:
		display->displayError(ERROR_INSUFFICIENT_RAM);
		return NULL;
	}

	AudioClip* newClip = new (clipMemory) AudioClip();

	newClip->setupForRecordingAsAutoOverdub(this, modelStackOldClip->song, newOverdubNature);

	char modelStackMemoryNewClip[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackNewClip =
	    setupModelStackWithTimelineCounter(modelStackMemoryNewClip, modelStackOldClip->song, newClip);

	int32_t error = newClip->setOutput(modelStackNewClip, output, this);

	if (error) {
		newClip->~AudioClip();
		GeneralMemoryAllocator::get().dealloc(clipMemory);
		goto ramError;
	}

#if ALPHA_OR_BETA_VERSION
	if (!newClip->paramManager.summaries[0].paramCollection) {
		display->freezeWithError("E421"); // Trying to diversify Leo's E410
	}
#endif

	return newClip;
}

bool AudioClip::cloneOutput(ModelStackWithTimelineCounter* modelStack) {
	//don't clone for loop commands in red mode
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

	// If we have a recorder that's gotten into error/aborted state, but we haven't registered that here yet, do that now. This isn't really the ideal place for this...
	if (recorder && recorder->status == RECORDER_STATUS_ABORTED) {
		abortRecording();
	}

	// If at pos 0, that's the only place where anything really important happens: play the Sample
	if (!lastProcessedPos) {

		// If there is a sample, play it
		if (sampleHolder.audioFile && !((Sample*)sampleHolder.audioFile)->unplayable) {

			guide.sequenceSyncStartedAtTick =
			    playbackHandler
			        .lastSwungTickActioned; // Must do this even if we're going to return due to time stretching being active

			// Obviously if no voiceSample yet, need to reset envelope for first usage. Otherwise,
			// If the envelope's now releasing (e.g. from playback just being stopped and then restarted, e.g. by pressing <> + play),
			// and if not doing a late start (which would cause more sound to start happening after the envelope finishes releasing),
			// then we'd absolutely better reset that envelope because there's no other way any sound's going to come out!
			bool shouldResetEnvelope =
			    !voiceSample
			    || (((AudioOutput*)output)->envelope.state >= EnvelopeStage::RELEASE
			        && !doingLateStart); // Come to think of it, would doingLateStart ever (or normally) be true if we're at play pos 0?

			// If already had a VoiceSample, everything's probably all fine...
			if (voiceSample) {

				// But, if it's reading from (or writing to) a cache, we have to manually cut it out right now and tell it to restart, so the new
				// play-through will remain perfectly synced up to this start-tick we're at now.
				if (voiceSample->cache

				    // Or, if no time-stretcher, which also means we're not "fudging" - probably because there was no pre-margin -
				    // then yup we'll wanna force restart now too - because AudioClip VoiceSamples don't obey their loop points,
				    // so it won't have happened otherwise.
				    || !voiceSample->timeStretcher

				    // Or, if time stretching is on but the "newer" play-head is no longer active - because it's shot past the end of
				    // the waveform, and this waveform didn't have an extra "margin" at the end, then we want to just cut everything and start from scratch
				    // again.
				    || !voiceSample->timeStretcher->playHeadStillActive[PLAY_HEAD_NEWER]) {

					// Yup, do that unassignment
doUnassignment:
					voiceSample->beenUnassigned();
				}

				// Or if none of those several conditions were met...
				else {
					// If here, we know time stretching is on.

					// If no pre-margin, then still do go and do the unassignment and start afresh - cos we'll wanna hear that sharp start-point perfectly rather than just fading into it after a play-head reaches its end
					uint32_t waveformStartByte = ((Sample*)sampleHolder.audioFile)->audioDataStartPosBytes;
					if (sampleControls.reversed) {
						waveformStartByte +=
						    ((Sample*)sampleHolder.audioFile)->audioDataLengthBytes
						    - sampleHolder.audioFile->numChannels
						          * ((Sample*)sampleHolder.audioFile)
						                ->byteDepth; // The actual first sample of the waveform in our given direction, regardless of our elected start-point
					}
					int32_t numBytesOfPreMarginAvailable =
					    (int32_t)(guide.getBytePosToStartPlayback(true) - waveformStartByte);
					if (sampleControls.reversed) {
						numBytesOfPreMarginAvailable = -numBytesOfPreMarginAvailable;
					}
					if (numBytesOfPreMarginAvailable <= 0) {
						goto doUnassignment;
					}

					// If we were "fudging" a time-stretch just to get a free crossfade, then we can now stop doing all of that. (It should automatically stop eventually anyway, but let's be clean and efficient and just kill it now.)
					if (voiceSample->fudging) {
						voiceSample->endTimeStretching();
					}
					// Otherwise, if we're just regular time-stretching (not for mere "fudging" reasons), don't do anything and just get out. The regular time-stretching algorithm takes care of causing playback to jump back to the start of the Sample.

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
		display->freezeWithError("E430");
	}
#endif

	if (!sampleHolder.audioFile || ((Sample*)sampleHolder.audioFile)->unplayable) {
		return;
	}

	// If reading or writing cache, that's not gonna be valid now that we've moved our play position, so gotta stop that.
	if (voiceSample && voiceSample->cache) {
		int32_t priorityRating = 1;
		bool success = voiceSample->stopUsingCache(&guide, ((Sample*)sampleHolder.audioFile), priorityRating,
		                                           getLoopingType(modelStack) == LoopType::LOW_LEVEL);
		if (!success) {
			unassignVoiceSample();
		}
	}

	// For synced time-stretching, that syncing is done by noting what the internal tick count "was" at the start of the Clip, so we work that out here.
	// However, due to the efficient ticking system and the fact that the clip length may have just been reduced, it's possible that this would
	// then give us a result which would indicate that our current play position is actually beyond the length of the Clip.
	// So, we do some wrapping here to ensure that guide.sequenceSyncStartedAtTickTrivialValue is not longer ago (from the "actual" current internal ticket time)
	// than the length of the Clip. Despite the fact that we generally here deal with the positions / counts of the last swung tick, which is why this problem exists in the first place.
	// To be clear, where wrapping occurs, guide.sequenceSyncStartedAtTickTrivialValue will be after the last actioned swung tick.
	int32_t sequenceSyncStartedAtTickTrivialValue = playbackHandler.lastSwungTickActioned - lastProcessedPos;
	int32_t currentInternalTickCount = playbackHandler.getCurrentInternalTickCount();
	int32_t sequenceSyncStartedNumTicksAgo = currentInternalTickCount - sequenceSyncStartedAtTickTrivialValue;
	if (sequenceSyncStartedNumTicksAgo < 0) { // Shouldn't happen
		if (ALPHA_OR_BETA_VERSION) {
			display->freezeWithError("nofg"); // Ron got, Nov 2021. Wait no, he didn't have playback on!
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
	// Also can only do this if envelope not releasing, which (possibly not anymore since I fixed other bug) it can still be if this is our second quick successive <>+play starting playback partway through Clip
	if (voiceSample && voiceSample->timeStretcher && ((AudioOutput*)output)->envelope.state < EnvelopeStage::RELEASE) {
		return;
	}

	// Ok, get playback all set up
	doingLateStart = true;
	maySetupCache = false;

	// If already had a VoiceSample, we can reuse it
	if (voiceSample) {

		// But we're gonna do a nice quick fade-out first.
		// TODO: probably not super necessary now we've got time-stretching taking care of sorta doing a crossfade, above.
		// We'd only actually need to do any such fade manually if we weren't time-stretching before, and we're also not going to be after
		// (though it'd be hard to predict whether we're going to be after)
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
		guide.sequenceSyncLengthTicks = loopLength;
		guide.setupPlaybackBounds(sampleControls.reversed);
	}
}

void AudioClip::sampleZoneChanged(ModelStackWithTimelineCounter const* modelStack) {
	if (voiceSample) {

		int32_t priorityRating = 1; // probably better fix this...

		voiceSample->sampleZoneChanged(&guide, ((Sample*)sampleHolder.audioFile), MarkerType::END,
		                               getLoopingType(modelStack), priorityRating, true);
	}
}

int64_t AudioClip::getNumSamplesTilLoop(ModelStackWithTimelineCounter* modelStack) {

	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(0, NULL);

	int32_t cutPos = modelStackWithNoteRow->getPosAtWhichPlaybackWillCut();
	int32_t loopPosWithinClip = std::min(cutPos, loopLength);

	int32_t ticksTilLoop = loopPosWithinClip - lastProcessedPos;
	uint32_t loopTime = playbackHandler.getInternalTickTime(playbackHandler.lastSwungTickActioned + ticksTilLoop);
	return loopTime - AudioEngine::audioSampleTimer;
}

void AudioClip::render(ModelStackWithTimelineCounter* modelStack, int32_t* outputBuffer, int32_t numSamples,
                       int32_t amplitude, int32_t amplitudeIncrement, int32_t pitchAdjust) {

	if (!voiceSample) {
		return;
	}

	Sample* sample = ((Sample*)sampleHolder.audioFile);

	// First, if we're still attempting to do a "late start", see if we can do that (perhaps not if relevant audio data hasn't loaded yet)
	if (doingLateStart && ((AudioOutput*)output)->envelope.state < EnvelopeStage::FAST_RELEASE) {
		uint64_t numSamplesIn = guide.getSyncedNumSamplesIn();

		int32_t result = voiceSample->attemptLateSampleStart(&guide, sample, numSamplesIn);
		if (result) {
			if (result == LATE_START_ATTEMPT_FAILURE) {
				unassignVoiceSample();
			}
			return;
		}

		doingLateStart = false;
	}

	int32_t timeStretchRatio = 16777216;
	int32_t phaseIncrement = sampleHolder.neutralPhaseIncrement;

	if (pitchAdjust != 16777216) {
		uint64_t newPhaseIncrement = ((uint64_t)phaseIncrement * (uint64_t)pitchAdjust) >> 24;
		phaseIncrement = (newPhaseIncrement > 2147483647) ? 2147483647 : newPhaseIncrement;
	}

	uint32_t sampleLengthInSamples = sampleHolder.getDurationInSamples(true); // In the sample rate of the file!
	uint32_t clipLengthInSamples =
	    (playbackHandler.getTimePerInternalTickBig() * loopLength) >> 32; // We haven't rounded... should we?

	// To stop things getting insane, limit to 32x speed
	//if ((sampleLengthInSamples >> 5) > clipLengthInSamples) return false;

	uint64_t requiredSpeedAdjustment = ((uint64_t)sampleLengthInSamples << 24) / clipLengthInSamples;

	// If we're squishing time...
	if (sampleControls.pitchAndSpeedAreIndependent) {
		timeStretchRatio = requiredSpeedAdjustment;

		// And if pitch was manually adjusted too (or file's sample rate wasn't 44100kHz, that's fine - counteract that by adjusting the time-stretch amount more
		if (phaseIncrement != 16777216) {
			timeStretchRatio = ((uint64_t)timeStretchRatio << 24) / (uint32_t)phaseIncrement;
		}

		// Or if no manual pitch adjustment...
		else {

			// If we'd only be time-stretching a tiiiny bit (+/- 1 cent)...
			if (timeStretchRatio >= 16661327 && timeStretchRatio < 16893911) {

				// And if playback has stopped or the envelope is doing a fast release before we begin another "late start"...
				if (!playbackHandler.isEitherClockActive() || doingLateStart) {
justDontTimeStretch:
					// We can just not time-stretch... for now, until we end up more out-of-sync later
					timeStretchRatio = 16777216;
				}

				// Or...
				else {

					// If we're less than 7.8mS out of sync, then that's another (even more common) reason not to time-stretch
					int32_t numSamplesLaggingBehindSync = guide.getNumSamplesLaggingBehindSync(voiceSample);
					int32_t numSamplesDrift = std::abs(numSamplesLaggingBehindSync);

					if (numSamplesDrift < (sample->sampleRate >> 7)) {
						goto justDontTimeStretch;
					}
					else {
						//Debug::print("sync: ");
						//Debug::println(numSamplesDrift);
					}
				}
			}
		}
	}

	// Or if we're squishing pitch...
	else {

		// If no prior pitch adjustment, we play back 100% natively, with no pitch shifting / time stretching. Just with pitch / speed changed like speeding/slowing a record
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
	// I don't love that this is being called for every AudioClip at every render. This is to determine the "looping" parameter
	// for the call to VoiceSample::render() below. It'd be better to maybe have that just query the currentPlaybackMode
	// when it needs to actually use this info, cos it only occasionally needs it

	bool stillActive;

	// If Clip will loop at end...
	if (clipWillLoopAtEnd) {

		// If no time-stretcher, and not reading cache, we might want to "fudge" to eliminate the click at the loop point (if it's gonna loop)
		if (timeStretchRatio == 16777216 && !voiceSample->timeStretcher
		    && (!voiceSample->cache || voiceSample->writingToCache)) {

			// First, see if there is actually any pre-margin at all for us to crossfade to

			int32_t bytesPerSample = sample->byteDepth * sample->numChannels;

			int32_t startByte = sample->audioDataStartPosBytes;
			if (guide.playDirection != 1) {
				startByte +=
				    sample->audioDataLengthBytes
				    - bytesPerSample; // The actual first sample of the waveform in our given direction, regardless of our elected start-point
			}

			int32_t numBytesOfPreMarginAvailable =
			    (int32_t)(guide.getBytePosToStartPlayback(true) - startByte) * guide.playDirection;

			if (numBytesOfPreMarginAvailable > 0) {

				int64_t numSamplesTilLoop = getNumSamplesTilLoop(modelStack);

				if (numSamplesTilLoop <= kAntiClickCrossfadeLength) {

					int32_t numSamplesOfPreMarginAvailable =
					    (uint32_t)numBytesOfPreMarginAvailable / (uint8_t)bytesPerSample;
					if (phaseIncrement != 16777216) {
						numSamplesOfPreMarginAvailable =
						    ((uint64_t)numSamplesOfPreMarginAvailable << 24) / (uint32_t)phaseIncrement;
					}

					if (numSamplesOfPreMarginAvailable > 2) {
						//Debug::println("");
						//Debug::println("might attempt fudge");

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

		// We want to do a fast release *before* the end, to finish right as the end is reached. So that any waveform after the end isn't heard.
		// TODO: in an ideal world, would we only do this if there actually is some waveform "margin" after the end that we want to avoid hearing, and otherwise just do the release right at the end (does that already happen, I forgot?)
		// It's perhaps a little bit surprising, but this even works and sounds perfect (you never hear any of the margin) when time-stretching is happening! Down to about half speed. Below that, you hear some of the margin.
		if (((AudioOutput*)output)->envelope.state < EnvelopeStage::FAST_RELEASE) {

			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(0, NULL);

			int32_t cutPos = modelStackWithNoteRow->getPosAtWhichPlaybackWillCut();
			if (cutPos < 2147483647) {

				int32_t ticksTilCut = cutPos - lastProcessedPos;
				uint32_t loopTime =
				    playbackHandler.getInternalTickTime(playbackHandler.lastSwungTickActioned + ticksTilCut);
				int32_t timeTilLoop = loopTime - AudioEngine::audioSampleTimer;

				if (timeTilLoop < 1024) {
					((AudioOutput*)output)
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

		stillActive = voiceSample->render(&guide, outputBuffer, numSamples, sample, sample->numChannels, loopingType,
		                                  phaseIncrement, timeStretchRatio, amplitude, amplitudeIncrement,
		                                  sampleControls.getInterpolationBufferSize(phaseIncrement),
		                                  sampleControls.interpolationMode, 1);
	}

	if (!stillActive) {
doUnassign:
		unassignVoiceSample();
	}
}

// Returns the "looping" parameter that gets passed into a lot of functions.
LoopType AudioClip::getLoopingType(ModelStackWithTimelineCounter const* modelStack) {

	// We won't loop at the low level. We may want to loop at time-stretcher level, in the following case (not if the end-point is set to beyond the waveform's length).

	bool shouldLoop =
	    (sampleControls.reversed || sampleHolder.endPos <= ((Sample*)sampleHolder.audioFile)->lengthInSamples)
	    && currentPlaybackMode->willClipContinuePlayingAtEnd(modelStack);

	return shouldLoop ? LoopType::TIMESTRETCHER_LEVEL_IF_ACTIVE : LoopType::NONE;

	// ---- This is the old comment / logic we previously had, here
	// Normally we'll loop at the lowest level - but not if user has inserted silence at the end
	// (put the end-pos beyond the end of the Sample)

	// return (sampleControls.reversed || sampleHolder.endPos <= ((Sample*)sampleHolder.audioFile)->lengthInSamples);

	// Note that the actual "loop points" don't get obeyed for AudioClips - if any looping happens at the low level,
	// it'll only be at the very end of the waveform if we happen to reach it.
	// But, looping may still happen as normally expected at the TimeStretcher level...
}

void AudioClip::unassignVoiceSample() {
	if (voiceSample) {
		voiceSample->beenUnassigned();
		AudioEngine::voiceSampleUnassigned(voiceSample);
		voiceSample = NULL;
	}
}

void AudioClip::expectNoFurtherTicks(Song* song, bool actuallySoundChange) {

	// If it's actually another Clip, that we're recording into the arranger...
	if (output->activeClip && output->activeClip->beingRecordedFromClip == this) {
		output->activeClip->expectNoFurtherTicks(song, actuallySoundChange);
		return;
	}

	if (voiceSample) {
		if (actuallySoundChange) {

			// Fix only added for bug / crash discovered in Feb 2021!
			if (doingLateStart) {
				// If waiting to do a late start, and we're not waiting for a past bit to fade out, well there's no sound right now, so just cut out.
				if (((AudioOutput*)output)->envelope.state < EnvelopeStage::FAST_RELEASE) {
					unassignVoiceSample();
				}

				// Or if we were planning to do a late start as soon as the current sound fades out, then just abandon the late start, but keep doing the fade
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

	Clip::posReachedEnd(modelStack);

	// If recording from session to arranger...
	if (playbackHandler.recording == RECORDING_ARRANGEMENT && isArrangementOnlyClip()) {

		Debug::println("");
		Debug::print("AudioClip::posReachedEnd, at pos: ");
		Debug::println(playbackHandler.getActualArrangementRecordPos());

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

		int32_t error = beingRecordedFromClip->clone(modelStack); // Puts the new Clip in the modelStack.
		if (error) {
			return;
		}

		Clip* newClip = (Clip*)modelStack->getTimelineCounter();

		newClip->beingRecordedFromClip = beingRecordedFromClip;
		beingRecordedFromClip = NULL;

		newClip->section = 255;

		modelStack->song->arrangementOnlyClips.insertClipAtIndex(newClip, 0); // Can't fail - checked above

		clipInstanceI++;

		error = output->clipInstances.insertAtIndex(clipInstanceI); // Shouldn't be able to fail...
		if (error) {
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
	unassignVoiceSample();

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
			int32_t error = newParamManager.cloneParamCollectionsFrom(&paramManager, true);
			if (error) {
				goto doNormal; // If out of RAM, leave ParamManager behind
			}

			song->backUpParamManager((ModControllableAudio*)output->toModControllable(), NULL, &newParamManager);
			// Obscure bug fixed, Oct 2022. Previously, we filed it away under not just the ModControllable, but also this Clip.
			// Perhaps that's still theoretically what we'd like to do? But, it caused an E170 because when we do a song swap and call
			// Song::deleteSoundsWhichWontSound(), that calls deleteAllBackedUpParamManagersWithClips(), which would then delete this ParamManager here,
			// because we gave it a Clip. But it's actually still needed by the associated ModControllable / AudioOutput.
		}
	}

	if (!shouldRetainLinksToOutput) {
		output = NULL;
	}
}

int64_t AudioClip::getSamplesFromTicks(int32_t ticks) {
	if (recorder) {
		return playbackHandler.getTimePerInternalTickFloat() * ticks;
	}
	else {
		return (int64_t)ticks * sampleHolder.getDurationInSamples(true)
		       / loopLength; // Yup, ticks could be negative, and so could the result
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

		if (sampleControls.reversed) {
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

	//Debug::println("AudioClip::renderAsSingleRow");

	Sample* sample;
	if (recorder) {
		sample =
		    ((recorder->status == RECORDER_STATUS_ABORTED || recorder->reachedMaxFileSize) ? NULL : recorder->sample);
	}
	else {
		sample = ((Sample*)sampleHolder.audioFile);
	}

	if (sample) {

		int64_t xScrollSamples;
		int64_t xZoomSamples;

		getScrollAndZoomInSamples(xScroll, xZoom, &xScrollSamples, &xZoomSamples);

		RGB rgb = getColour();

		bool success = waveformRenderer.renderAsSingleRow(sample, xScrollSamples, xZoomSamples, image, &renderData,
		                                                  recorder, rgb, sampleControls.reversed, xStart, xEnd);

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

void AudioClip::writeDataToFile(Song* song) {

	storageManager.writeAttribute("trackName", output->name.get());

	storageManager.writeAttribute("filePath", sampleHolder.audioFile ? sampleHolder.audioFile->filePath.get()
	                                                                 : sampleHolder.filePath.get());
	storageManager.writeAttribute("startSamplePos", sampleHolder.startPos);
	storageManager.writeAttribute("endSamplePos", sampleHolder.endPos);
	storageManager.writeAttribute("pitchSpeedIndependent", sampleControls.pitchAndSpeedAreIndependent);
	if (sampleControls.interpolationMode == InterpolationMode::LINEAR) {
		storageManager.writeAttribute("linearInterpolation", 1);
	}
	if (sampleControls.reversed) {
		storageManager.writeAttribute("reversed", "1");
	}
	storageManager.writeAttribute("attack", attack);
	storageManager.writeAttribute("priority", util::to_underlying(voicePriority));

	if (sampleHolder.transpose) {
		storageManager.writeAttribute("transpose", sampleHolder.transpose);
	}
	if (sampleHolder.cents) {
		storageManager.writeAttribute("cents", sampleHolder.cents);
	}

	storageManager.writeAttribute("overdubsShouldCloneAudioTrack", overdubsShouldCloneOutput);

	Clip::writeDataToFile(song);

	storageManager.writeOpeningTagBeginning("params");
	GlobalEffectableForClip::writeParamAttributesToFile(&paramManager, true);
	storageManager.writeOpeningTagEnd();
	GlobalEffectableForClip::writeParamTagsToFile(&paramManager, true);
	storageManager.writeClosingTag("params");
}

int32_t AudioClip::readFromFile(Song* song) {

	int32_t error;

	if (false) {
ramError:
		error = ERROR_INSUFFICIENT_RAM;

someError:
		return error;
	}

	char const* tagName;

	int32_t readAutomationUpToPos = kMaxSequenceLength;

	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		//Debug::println(tagName); delayMS(30);

		if (!strcmp(tagName, "trackName")) {
			storageManager.readTagOrAttributeValueString(&outputNameWhileLoading);
		}

		else if (!strcmp(tagName, "filePath")) {
			storageManager.readTagOrAttributeValueString(&sampleHolder.filePath);
		}

		else if (!strcmp(tagName, "overdubsShouldCloneAudioTrack")) {
			overdubsShouldCloneOutput = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "startSamplePos")) {
			sampleHolder.startPos = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "endSamplePos")) {
			sampleHolder.endPos = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "pitchSpeedIndependent")) {
			sampleControls.pitchAndSpeedAreIndependent = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "linearInterpolation")) {
			if (storageManager.readTagOrAttributeValueInt()) {
				sampleControls.interpolationMode = InterpolationMode::LINEAR;
			}
		}

		else if (!strcmp(tagName, "attack")) {
			attack = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "priority")) {
			voicePriority = static_cast<VoicePriority>(storageManager.readTagOrAttributeValueInt());
		}

		else if (!strcmp(tagName, "reversed")) {
			sampleControls.reversed = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "transpose")) {
			sampleHolder.transpose = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "cents")) {
			sampleHolder.cents = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "params")) {
			paramManager.setupUnpatched();
			GlobalEffectableForClip::initParams(&paramManager);
			GlobalEffectableForClip::readParamsFromFile(&paramManager, readAutomationUpToPos);
		}

		else {
			readTagFromFile(tagName, song, &readAutomationUpToPos);
		}

		storageManager.exitTag();
	}

	return NO_ERROR;
}

int32_t AudioClip::claimOutput(ModelStackWithTimelineCounter* modelStack) {

	output = modelStack->song->getAudioOutputFromName(&outputNameWhileLoading);

	if (!output) {
		return ERROR_FILE_CORRUPTED;
	}

	return NO_ERROR;
}

void AudioClip::loadSample(bool mayActuallyReadFile) {
	int32_t error = sampleHolder.loadFile(sampleControls.reversed, false, mayActuallyReadFile);
	if (error) {
		display->displayError(error);
	}
}

// Keeps same ParamManager
int32_t AudioClip::changeOutput(ModelStackWithTimelineCounter* modelStack, Output* newOutput) {
	detachAudioClipFromOutput(modelStack->song, false, true);

	return setOutput(modelStack, newOutput);
}

int32_t AudioClip::setOutput(ModelStackWithTimelineCounter* modelStack, Output* newOutput,
                             AudioClip* favourClipForCloningParamManager) {
	output = newOutput;
	int32_t error = solicitParamManager(modelStack->song, NULL, favourClipForCloningParamManager);
	if (error) {
		return error;
	}

	outputChanged(modelStack, newOutput);

	return NO_ERROR;
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
	lengthChanged(modelStack, oldLength, NULL);
}

bool AudioClip::currentlyScrollableAndZoomable() {
	bool shouldLock = (getCurrentlyRecordingLinearly()
	                   && (!playbackHandler.isEitherClockActive() || currentPlaybackMode == &arrangement));
	return !shouldLock;
}

void AudioClip::clear(Action* action, ModelStackWithTimelineCounter* modelStack) {
	Clip::clear(action, modelStack);

	// If recording, stop that - but only if we're not doing tempoless recording
	if (recorder) {
		if (!playbackHandler.isEitherClockActive()) {
			abortRecording();
		}
	}

	else if (sampleHolder.audioFile) {

		unassignVoiceSample();

		if (action) {
			// Must happen first
			action->recordAudioClipSampleChange(this);
		}

		sampleHolder.filePath.clear();
		sampleHolder.setAudioFile(NULL);
	}

	renderData.xScroll = -1;
}

bool AudioClip::getCurrentlyRecordingLinearly() {
	return recorder;
}

void AudioClip::setPos(ModelStackWithTimelineCounter* modelStack, int32_t newPos, bool useActualPosForParamManagers) {

	Clip::setPos(modelStack, newPos, useActualPosForParamManagers);

	setPosForParamManagers(modelStack, useActualPosForParamManagers);
}

bool AudioClip::shiftHorizontally(ModelStackWithTimelineCounter* modelStack, int32_t amount) {
	// No horizontal shift when recording
	if (recorder)
		return false;
	// No horizontal shift when no sample is loaded
	if (!sampleHolder.audioFile)
		return false;

	int64_t newStartPos = int64_t(sampleHolder.startPos) - getSamplesFromTicks(amount);
	uint64_t sampleLength = ((Sample*)sampleHolder.audioFile)->lengthInSamples;

	if (newStartPos < 0 || newStartPos > sampleLength) {
		return false;
	}

	if (paramManager.containsAnyParamCollectionsIncludingExpression()) {
		paramManager.shiftHorizontally(
		    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager), amount, loopLength);
	}

	uint64_t length = sampleHolder.endPos - sampleHolder.startPos;

	// Stop the clip if it is playing
	bool active = (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(this) && voiceSample);
	unassignVoiceSample();

	sampleHolder.startPos = newStartPos;
	sampleHolder.endPos = newStartPos + length;

	sampleHolder.claimClusterReasons(sampleControls.reversed, CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE);

	if (active) {
		expectEvent();
		reGetParameterAutomation(modelStack);

		// Resume the clip if it was playing before
		currentSong->currentClip->resumePlayback(modelStack, true);
	}
	return true;
}

uint64_t AudioClip::getCullImmunity() {
	uint32_t distanceFromEnd = loopLength - getLivePos();
	// We're gonna cull time-stretching ones first
	bool doingTimeStretching = (voiceSample && voiceSample->timeStretcher);
	return ((uint64_t)voicePriority << 33) + ((uint64_t)!doingTimeStretching << 32) + distanceFromEnd;
}
