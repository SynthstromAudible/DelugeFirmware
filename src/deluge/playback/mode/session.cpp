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

#include "gui/views/arranger_view.h"
#include "processing/engines/audio_engine.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "gui/views/instrument_clip_view.h"
#include "processing/sound/sound_instrument.h"
#include "playback/mode/session.h"
#include "definitions.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "gui/views/view.h"
#include "model/note/note_row.h"
#include "io/uart/uart.h"
#include "model/drum/drum.h"
#include "model/action/action_logger.h"
#include "model/action/action.h"
#include "io/midi/midi_engine.h"
#include "processing/engines/cv_engine.h"
#include <string.h>
#include "gui/views/session_view.h"
#include "gui/ui/audio_recorder.h"
//#include <algorithm>
#include "playback/mode/arrangement.h"
#include "gui/views/audio_clip_view.h"
#include "storage/storage_manager.h"
#include "model/clip/audio_clip.h"
#include "util/container/hashtable/open_addressing_hash_table.h"
#include "hid/buttons.h"
#include "hid/display/numeric_driver.h"
#include "model/model_stack.h"
#include "gui/ui/load/load_song_ui.h"

Session session;

#define LAUNCH_STATUS_NOTHING_TO_SYNC_TO 0
#define LAUNCH_STATUS_LAUNCH_USING_QUANTIZATION 1
#define LAUNCH_STATUS_LAUNCH_ALONG_WITH_EXISTING_LAUNCHING 2

Session::Session() {
	cancelAllLaunchScheduling();
	lastSectionArmed = 255;
}

void Session::armAllClipsToStop(int afterNumRepeats) {

	Clip* waitForClip = currentSong->getLongestClip(false, true);

	if (!waitForClip) return; // Nothing to do if no Clips are playing

	uint32_t quantization;
	uint32_t currentPosWithinQuantization;
	uint8_t launchStatus =
	    investigateSyncedLaunch(waitForClip, &currentPosWithinQuantization, &quantization, 0xFFFFFFFF, false);

	if (launchStatus == LAUNCH_STATUS_NOTHING_TO_SYNC_TO) {
		// We'd never actually get here, because there always are Clips playing if this function gets called I think
	}

	else if (launchStatus == LAUNCH_STATUS_LAUNCH_USING_QUANTIZATION) {
		int32_t pos = currentPosWithinQuantization % quantization;
		int32_t ticksTilSwap = quantization - pos;
		scheduleLaunchTiming(playbackHandler.getActualSwungTickCount() + ticksTilSwap, afterNumRepeats, quantization);
	}

	else if (launchStatus == LAUNCH_STATUS_LAUNCH_ALONG_WITH_EXISTING_LAUNCHING) {
		// Nothing to do!
	}

	// If any soloing Clips
	if (currentSong->getAnyClipsSoloing()) {
		for (int l = 0; l < currentSong->sessionClips.getNumElements(); l++) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(l);
			clip->activeIfNoSolo = false;
			if (clip->soloingInSessionMode) clip->armState = ARM_STATE_ON_NORMAL;
		}
	}

	// Or if no soloing Clips
	else {
		for (int l = 0; l < currentSong->sessionClips.getNumElements(); l++) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(l);
			clip->armState = clip->activeIfNoSolo ? ARM_STATE_ON_NORMAL : ARM_STATE_OFF;
		}
	}
}

void Session::armNextSection(int oldSection, int numRepetitions) {

	if (numRepetitions == -1) numRepetitions = currentSong->sections[oldSection].numRepetitions;
	if (currentSong->sessionClips.getClipAtIndex(0)->section != oldSection) {

		for (int c = 1; c < currentSong->sessionClips.getNumElements(); c++) { // NOTE: starts at 1, not 0
			Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

			if (clip->section == oldSection) {
				int newSection =
				    currentSong->sessionClips.getClipAtIndex(c - 1)->section; // Grab section from next Clip down
				userWantsToArmClipsToStartOrSolo(newSection, NULL, true, false, false, numRepetitions, false);
				lastSectionArmed = newSection;
				return;
			}
		}
	}

	// If we're here, that was the last section
	armAllClipsToStop(numRepetitions);
	lastSectionArmed = 254;
}

// Returns whether it began
bool Session::giveClipOpportunityToBeginLinearRecording(Clip* clip, int clipIndex, int buttonPressLatency) {

	if (playbackHandler.recording == RECORDING_ARRANGEMENT) return false; // Not allowed if recording to arranger

	bool currentClipHasSameOutput =
	    (currentSong->currentClip
	     && currentSong->currentClip->output
	            == clip->output); // Must do this before calling opportunityToBeginLinearLoopRecording(), which may clone a new Output

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);

	bool newOutputCreated;
	bool begun = clip->opportunityToBeginSessionLinearRecording(modelStack, &newOutputCreated,
	                                                            buttonPressLatency); // May create new Output

	if (begun) {

		if (getRootUI() == &sessionView) {
			sessionView.clipNeedsReRendering(clip); // Necessary for InstrumentClips
		}

		// If currently looking at the old clip, teleport us to the new one
		else if (currentClipHasSameOutput && getCurrentUI()->toClipMinder()) {

			currentSong->currentClip = clip;
			getCurrentUI()->focusRegained(); // A bit shifty...

			uiNeedsRendering(getCurrentUI());
		}

		if (clip->overdubNature != OVERDUB_NORMAL && playbackHandler.isEitherClockActive()) {
			armClipToStopAction(clip);

			// Create new clip if we're continuous-layering
			if (clip->getCurrentlyRecordingLinearly() && clip->overdubNature == OVERDUB_CONTINUOUS_LAYERING) {
				currentSong->createPendingNextOverdubBelowClip(clip, clipIndex,
				                                               OVERDUB_CONTINUOUS_LAYERING); // Make it spawn more too
			}
		}
	}

	if (newOutputCreated) {

		if (getRootUI() == &arrangerView) {

			if (getCurrentUI() == &arrangerView) {
				arrangerView.exitSubModeWithoutAction();
			}

			arrangerView.repopulateOutputsOnScreen(true);
		}
	}

	return begun;
}

void Session::doLaunch() {

	view.flashPlayDisable();
	currentSong->deactivateAnyArrangementOnlyClips(); // In case any still playing after switch from arrangement

	bool anyLinearRecordingBefore = false;
	bool anySoloingAfter = false;
	bool anyClipsStillActiveAfter = false;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	OpenAddressingHashTableWith32bitKey outputsLaunchedFor;

	// First do a loop through all Clips seeing which ones are going to launch, so we can then go through again and deactivate those Outputs' other Clips
	for (int c = currentSong->sessionClips.getNumElements() - 1; c >= 0; c--) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

		// Just gather a tiny bit of other info while we're at it
		anyLinearRecordingBefore = (anyLinearRecordingBefore || clip->getCurrentlyRecordingLinearly());

		// If this one's gonna launch to become active on its output (i.e. not gonna clone its output) when it wasn't before...
		if (clip->armState && !currentSong->isClipActive(clip)
		    && (!clip->isPendingOverdub || !clip->willCloneOutputForOverdub())) {

			Output* output = clip->output;

			bool alreadyLaunchedFor = false;
			outputsLaunchedFor.insert((uint32_t)output, &alreadyLaunchedFor);

			// If already seen another Clip to launch with same Output...
			if (alreadyLaunchedFor) {

				// No need to make note of that again, but we do get dibs if we're gonna be soloing
				if (clip->armState == ARM_STATE_ON_TO_SOLO) output->isGettingSoloingClip = true;
			}

			// Or if haven't yet...
			else {
				output->alreadyGotItsNewClip = false;
				output->isGettingSoloingClip = (clip->armState == ARM_STATE_ON_TO_SOLO);
			}
		}

		if (clip->soloingInSessionMode) {

			// If it's not armed, or its arming is just to stop recording, then it's still gonna be soloing afterwards
			if (!clip->armState || clip->getCurrentlyRecordingLinearly()) {
yesSomeSoloingAfter:
				anySoloingAfter = true;
yesSomeActiveAfter:
				anyClipsStillActiveAfter = true;
			}
		}
		else {
			if (clip->armState == ARM_STATE_ON_TO_SOLO) goto yesSomeSoloingAfter;

			else if (clip->armState == ARM_STATE_ON_NORMAL) {
				if (!clip->activeIfNoSolo || clip->soloingInSessionMode || clip->getCurrentlyRecordingLinearly()) {
					goto yesSomeActiveAfter;
				}
			}

			else { // Not armed
				if (clip->soloingInSessionMode || clip->activeIfNoSolo) {
					goto yesSomeActiveAfter;
				}
			}
		}
	}

	// Normally it's enough that we set alreadyGotItsNewClip and isGettingSoloingClip on just the Outputs who have a Clip launching.
	// But in the case where soloing is stopping entirely, other Clips are going to be launching, so we'll need to actually set these on all Outputs.
	if (!anySoloingAfter && currentSong->anyClipsSoloing) {
		for (Output* output = currentSong->firstOutput; output; output = output->next) {
			output->alreadyGotItsNewClip = false;
			output->isGettingSoloingClip = false;
		}
	}

	// Ok, now action the stopping of all Clips which need to stop - including ones which weren't actually armed to stop but need to stop in order to
	// make way for other ones which were armed to start.
	// But we can't action the starting of any Clips yet, until all stopping is done.
	for (int c = currentSong->sessionClips.getNumElements() - 1; c >= 0; c--) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

		Output* output = clip->output;

		clip->wasActiveBefore = currentSong->isClipActive(clip);

		// If active now (same as before cos we haven't changed it yet)...
		if (clip->wasActiveBefore) {

			bool stoppedLinearRecording = false;

			// If armed to solo...
			if (clip->armState == ARM_STATE_ON_TO_SOLO) {

				// We were active before, and we'll still be active, so no big change, just this:
				clip->soloingInSessionMode = true;
				clip->armState = ARM_STATE_OFF;

				// If wanting to stop recording linearly at the same time as that...
				if (clip->getCurrentlyRecordingLinearly()) {
					clip->finishLinearRecording(
					    modelStackWithTimelineCounter,
					    NULL); // Won't be a pending overdub - those aren't allowed if we're gonna be soloing
					stoppedLinearRecording = true;
				}
			}

			// If armed to stop
			else if (clip->armState) {

				clip->armState = ARM_STATE_OFF;

				// If output-recording (resampling) is stopping, we don't actually want to deactivate this Clip
				if (playbackHandler.stopOutputRecordingAtLoopEnd) {
					goto probablyJustKeepGoing;
				}

				// Recording linearly
				if (clip->getCurrentlyRecordingLinearly()) {
doFinishLinearRecording:
					Clip* nextPendingOverdub = currentSong->getPendingOverdubWithOutput(clip->output);
					if (nextPendingOverdub) {
						nextPendingOverdub->copyBasicsFrom(
						    clip); // Copy this again, in case it's changed since it was created
					}

					clip->finishLinearRecording(modelStackWithTimelineCounter, nextPendingOverdub);
					stoppedLinearRecording = true;

					// After finishing recording linearly, normally we just keep playing.
					goto probablyJustKeepGoing;
				}

				// Or, all other cases
				else {

					// If stopping soloing...
					if (clip->soloingInSessionMode) {

						clip->soloingInSessionMode = false;

						if (anySoloingAfter) {
							goto becameInactiveBecauseOfAnotherClipSoloing;
						}
						else {
							if (clip->activeIfNoSolo) {
								goto probablyJustKeepGoing;
							}
							else {
								goto becameInactiveBecauseOfAnotherClipSoloing;
							}
						}
					}

becameInactiveNormally:
					clip->activeIfNoSolo = false;

becameInactiveBecauseOfAnotherClipSoloing:
					clip->expectNoFurtherTicks(currentSong, true);

					if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
						clip->getClipToRecordTo()->endInstance(playbackHandler.getActualArrangementRecordPos(), true);
					}
				}
			}

			// Or if not armed, check we're allowed to still be going
			else {

probablyJustKeepGoing:
				// If some other Clip is gonna start soloing
				if (!currentSong->anyClipsSoloing && anySoloingAfter) {
					if (!stoppedLinearRecording && clip->getCurrentlyRecordingLinearly()) {
						goto doFinishLinearRecording;
					}
					else {
						goto becameInactiveBecauseOfAnotherClipSoloing; // Specifically do not change clip->activeIfNoSolo!
					}
				}

				// If some other Clip is launching for this Output, we gotta stop
				if (outputsLaunchedFor.lookup((uint32_t)output)) {

					// If we're linearly recording, we want to stop that as well as ceasing to be active
					if (!stoppedLinearRecording && clip->getCurrentlyRecordingLinearly()) {
						goto doFinishLinearRecording;
					}
					else {
						if (clip->soloingInSessionMode) {
							clip->soloingInSessionMode = false;
							goto becameInactiveBecauseOfAnotherClipSoloing; // Specifically do not change clip->activeIfNoSolo!
						}
						else {
							goto becameInactiveNormally;
						}
					}
				}

				// Otherwise, no action needed - this Clip can just keep being active
			}
		}
	}

	// Now's the point where old linear recording has ended, and new is yet to begin. So separate any Actions, for separate undoability
	actionLogger.closeAction(ACTION_RECORD);

	bool sectionWasJustLaunched = (lastSectionArmed < 254);
	bool anyLinearRecordingAfter = false;
	int32_t distanceTilLaunchEvent = 0; // For if clips automatically armed cos they just started recording a loop

	// Now action the launching of Clips
	for (int c = currentSong->sessionClips.getNumElements() - 1; c >= 0; c--) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

		Output* output = clip->output;

		// If we didn't already deal with this Clip, meaning it wasn't active before this launch event...
		if (!clip->wasActiveBefore) {

			bool wasArmedToStartSoloing = (clip->armState == ARM_STATE_ON_TO_SOLO);

			// If it's not armed, normally nothing needs to happen of course - it can just stay inactive
			if (!clip->armState) {

				// But if other soloing has stopped and we're suddenly to become active as a result...
				if (!anySoloingAfter && clip->activeIfNoSolo && currentSong->anyClipsSoloing) {
					goto probablyBecomeActive;
				}
			}

			// But if it is armed, to start playing or soloing...
			else {

				clip->armState = ARM_STATE_OFF;

probablyBecomeActive:
				// If the Output already got its new Clip, then this Clip has missed out and can't become active on it
				if (output->alreadyGotItsNewClip) {
missedOut:
					// But, if we're a pending overdub that's going to clone its Output...
					if (clip->isPendingOverdub && clip->willCloneOutputForOverdub()) goto doNormalLaunch;

					clip->activeIfNoSolo = false;
				}

				// Otherwise, everything's fine and we can launch this Clip
				else {

					if (output->isGettingSoloingClip && !wasArmedToStartSoloing) goto missedOut;

					output->alreadyGotItsNewClip = true;

doNormalLaunch:
					clip->soloingInSessionMode = wasArmedToStartSoloing;
					if (!wasArmedToStartSoloing) clip->activeIfNoSolo = true;

					ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

					clip->setPos(modelStackWithTimelineCounter, 0, false);

					giveClipOpportunityToBeginLinearRecording(clip, c, 0);
					output = clip->output; // A new Output may have been created as recording began

					// If that caused it to be armed *again*...
					if (clip->armState == ARM_STATE_ON_NORMAL) {
						distanceTilLaunchEvent = getMax(distanceTilLaunchEvent, clip->loopLength);
					}

					output->setActiveClip(
					    modelStackWithTimelineCounter); // Must be after giveClipOpportunityToBeginLinearRecording(), cos this call clears any recorded-early notes

					if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
						clip->beginInstance(currentSong, playbackHandler.getActualArrangementRecordPos());
					}
				}
			}
		}

		bool clipActiveAfter = clip->soloingInSessionMode || (clip->activeIfNoSolo && !anySoloingAfter);

		if (clipActiveAfter) {
			anyLinearRecordingAfter = (anyLinearRecordingAfter || clip->getCurrentlyRecordingLinearly());
		}

		// If we found a playing Clip outside of the armed section, or vice versa, then we can't say we legitimately just launched a section
		if (clipActiveAfter != (clip->section == lastSectionArmed)) {
			sectionWasJustLaunched = false;
		}
	}

	currentSong->anyClipsSoloing = anySoloingAfter;

	// If some Clips are playing and they're all in the same section, we want to arm the next section
	if (sectionWasJustLaunched && currentSong->sections[lastSectionArmed].numRepetitions >= 1) {
		armNextSection(lastSectionArmed);
	}

	// Otherwise...
	else {

		bool sectionManuallyStopped = (lastSectionArmed == 254);

		lastSectionArmed = 255;

		// If no Clips active anymore...
		if (!anyClipsStillActiveAfter) {

			// If we're using the internal clock, we have the power to stop playback entirely
			if (playbackHandler.playbackState & PLAYBACK_CLOCK_INTERNAL_ACTIVE) {

				// If user is stopping resampling...
				if (playbackHandler.stopOutputRecordingAtLoopEnd) {
					playbackHandler.endPlayback();
				}

				// Or if the action was to manually stop all sections, which could happen if the last section in the song was playing, or if the user ended output recording with playback
				else if (sectionManuallyStopped) {

					// Stop playback entirely
					playbackHandler.endPlayback();

					int sectionToArm;
					// And re-activate the first section
					if (currentSong->sectionToReturnToAfterSongEnd < 254) {
						sectionToArm = currentSong->sectionToReturnToAfterSongEnd;
					}
					else {
						Clip* topClip =
						    currentSong->sessionClips.getClipAtIndex(currentSong->sessionClips.getNumElements() - 1);
						sectionToArm = topClip->section;
					}
					armSectionWhenNeitherClockActive(modelStack, sectionToArm, true);
					armingChanged();
				}
			}
		}

		// Or if some Clips still active...
		else {

			// If AudioClip recording just began...
			if (distanceTilLaunchEvent) {
				scheduleLaunchTiming(playbackHandler.lastSwungTickActioned + distanceTilLaunchEvent, 1,
				                     distanceTilLaunchEvent);
				armingChanged();
			}
		}
	}

	// If we were doing linear recording before, but we just stopped, then exit RECORD mode, as indicated on LED
	if (anyLinearRecordingBefore && !anyLinearRecordingAfter) {
		if (playbackHandler.recording == RECORDING_NORMAL) {
			playbackHandler.recording = RECORDING_OFF;
			playbackHandler.setLedStates();
		}
	}

	AudioEngine::bypassCulling = true;
}

void Session::justAbortedSomeLinearRecording() {
	if (playbackHandler.isEitherClockActive() && currentPlaybackMode == this) {

		for (int c = currentSong->sessionClips.getNumElements() - 1; c >= 0; c--) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

			if (clip->isPendingOverdub || clip->getCurrentlyRecordingLinearly()) return;
		}

		// Exit RECORD mode, as indicated on LED
		if (playbackHandler.recording == RECORDING_NORMAL) {
			playbackHandler.recording = RECORDING_OFF;
			playbackHandler.setLedStates();
		}
	}
}

void Session::scheduleLaunchTiming(int64_t atTickCount, int numRepeatsUntil, int32_t armedLaunchLengthForOneRepeat) {
	if (atTickCount > launchEventAtSwungTickCount) {
		playbackHandler.stopOutputRecordingAtLoopEnd = false;
		switchToArrangementAtLaunchEvent = false;
		launchEventAtSwungTickCount = atTickCount;
		numRepeatsTilLaunch = numRepeatsUntil;
		currentArmedLaunchLengthForOneRepeat = armedLaunchLengthForOneRepeat;

		int32_t ticksTilLaunchEvent = atTickCount - playbackHandler.lastSwungTickActioned;
		if (playbackHandler.swungTicksTilNextEvent > ticksTilLaunchEvent) {
			playbackHandler.swungTicksTilNextEvent = ticksTilLaunchEvent;
			playbackHandler.scheduleSwungTick();
		}
	}
}

void Session::cancelAllLaunchScheduling() {
	launchEventAtSwungTickCount = 0;
}

void Session::launchSchedulingMightNeedCancelling() {
	if (!preLoadedSong && !areAnyClipsArmed()) {
		cancelAllLaunchScheduling();
#if HAVE_OLED
		if (getCurrentUI() == &loadSongUI) loadSongUI.displayLoopsRemainingPopup(); // Wait, could this happen?
		else if (getRootUI() == &sessionView && !isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)) renderUIsForOled();
#else
		sessionView.redrawNumericDisplay();
#endif
	}
}

// Taking sync-scaling and the Clip's length into account, puts us at the place in the Clip as if playback had occurred under these conditions since the input clock started.
// Presumably we'd call this if the conditions have changed (e.g. sync-scaling changed) and we want to restore order
void Session::reSyncClipToSongTicks(Clip* clip) {

	if (clip->armState) {
		clip->armState = ARM_STATE_OFF;
		launchSchedulingMightNeedCancelling();
	}

	// If Clip inactive, nothing to do
	if (!currentSong->isClipActive(clip)) return;

	// I've sort of forgotten why this bit here is necessary (well, I know it deals with the skipping of ticks). Could it just be put into the setPos() function?
	// I basically copied this from Editor::launchClip()
	int32_t modifiedStartPos = (int32_t)playbackHandler.lastSwungTickActioned;
	while (modifiedStartPos < 0)
		modifiedStartPos += clip->loopLength; // Fairly unlikely I think

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);

	clip->setPos(modelStack, modifiedStartPos);

	clip->resumePlayback(modelStack);
}

// Will appropriately change a Clip's play-pos to sync it to input MIDI clock or another appropriate Clip's play-pos. Currently called only when a Clip is created or resized.
// In some cases, it'll determine that it doesn't want to resync the Clip - e.g. if there's nothing "nice" to sync it to. But if mustSetPosToSomething is supplied as true,
// then we'll make sure we still set the pos to something / sync it to something (because it presumably didn't have a valid pos yet).
// Check playbackHandler.isEitherClockActive() before calling this.
void Session::reSyncClip(ModelStackWithTimelineCounter* modelStack, bool mustSetPosToSomething, bool mayResumeClip) {

	Clip* clip = (Clip*)modelStack->getTimelineCounter();

	bool armingCancelled = clip->cancelAnyArming();
	if (armingCancelled) launchSchedulingMightNeedCancelling();

	if (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(clip)) {

		// If following external clock...
		if (playbackHandler.playbackState & PLAYBACK_CLOCK_EXTERNAL_ACTIVE) {

			// If this is in fact the sync-scaling Clip, we want to resync all Clips. The song will have already updated its inputTickMagnitude
			if (clip == modelStack->song->getSyncScalingClip()) {
				playbackHandler.resyncInternalTicksToInputTicks(modelStack->song);
			}

			// Otherwise, set its position according to the incoming clock count. (Wait, why on earth do I have it not syncing to other Clips in such cases? Some weird historical Deluge relic?)
			else {

				if (mustSetPosToSomething) {
doReSyncToSongTicks:
					reSyncClipToSongTicks(clip);
				}

				else {
					// ...but only if Clip length is a square multiple of input clock scaling
					uint32_t a = modelStack->song->getInputTickScale();
					while (a < clip->loopLength) {
						a <<= 1;
					}
					if (a == clip->loopLength) {
						goto doReSyncToSongTicks;
					}
				}
			}
		}

		// Or if playing from internal clock, then try to sync to another Clip with a similar-looking length (i.e. hopefully the same time signature)
		else {
			Clip* syncToClip =
			    modelStack->song->getLongestActiveClipWithMultipleOrFactorLength(clip->loopLength, false, clip);
			if (syncToClip) {

				int oldPos = clip->lastProcessedPos;
				clip->setPos(modelStack, syncToClip->getCurrentPosAsIfPlayingInForwardDirection());
				int newPos = clip->lastProcessedPos;

				// Only call "resume" if pos actually changed. This way, we can save some dropping out of AudioClips
				if (oldPos != newPos || mustSetPosToSomething) {
					if (mayResumeClip) clip->resumePlayback(modelStack);
				}
				else goto doAudioClipStuff;
			}
			else {
				if (mustSetPosToSomething) goto doReSyncToSongTicks;

				// For AudioClips, even if we're not gonna call resumePlayback(), we still need to do some other stuff if length has been changed (which it probably has if we're here)
doAudioClipStuff:
				if (clip->type == CLIP_TYPE_AUDIO) {
					((AudioClip*)clip)->setupPlaybackBounds();
					((AudioClip*)clip)->sampleZoneChanged(modelStack);
				}
			}
		}
	}
}

void Session::userWantsToUnsoloClip(Clip* clip, bool forceLateStart, int buttonPressLatency) {
	// If Deluge not playing session, easy
	if (!hasPlaybackActive()) {
doUnsolo:
		unsoloClip(clip);
	}

	// Or if Deluge *is* playing session...
	else {

		if (forceLateStart) {
			if (!playbackHandler.isEitherClockActive()) goto doTempolessRecording;
			else goto doUnsolo;
		}

		else {

			// Tempoless recording
			if (!playbackHandler.isEitherClockActive()) {
doTempolessRecording:
				if (clip->getCurrentlyRecordingLinearly()) { // Always true?
					playbackHandler.finishTempolessRecording(true, buttonPressLatency);
					return;
				}
			}

			// Otherwise, normal case - arm this Clip to stop soloing
			else {

				//armClips0(0, clip, false, forceLateStart); // Force "late start" if user holding shift button
				clip->armState = ARM_STATE_ON_NORMAL;
				int64_t wantToStopAtTime =
				    playbackHandler.getActualSwungTickCount()
				    - clip->getClipToRecordTo()->getActualCurrentPosAsIfPlayingInForwardDirection() + clip->loopLength;
				scheduleLaunchTiming(wantToStopAtTime, 1, clip->loopLength);
			}
		}
	}
}

// clipIndex is optional
void Session::cancelArmingForClip(Clip* clip, int* clipIndex) {
	clip->armState = ARM_STATE_OFF;

	if (clip->getCurrentlyRecordingLinearly()) {
		bool anyDeleted = currentSong->deletePendingOverdubs(clip->output, clipIndex);
		if (anyDeleted) uiNeedsRendering(&sessionView);
	}

	launchSchedulingMightNeedCancelling();
}

// Beware - calling this might insert or delete a Clip! E.g. if we disarm a Clip that had a pending overdub, the overdub will get deleted.
// clipIndex is optional.
void Session::toggleClipStatus(Clip* clip, int* clipIndex, bool doInstant, int buttonPressLatency) {

	// Not allowed if playing arrangement
	if (playbackHandler.playbackState && currentPlaybackMode == &arrangement) return;

	lastSectionArmed = 255;

	// If Clip armed, cancel arming - but not if it's an "instant" toggle
	if (clip->armState && !doInstant) {
		cancelArmingForClip(clip, clipIndex);
	}

	// If Clip soloing
	else if (clip->soloingInSessionMode) {
		userWantsToUnsoloClip(clip, doInstant, buttonPressLatency);
	}

	// Or, if some other Clip is soloed, just toggle the playing status - it won't make a difference
	else if (currentSong->getAnyClipsSoloing()) {
		clip->activeIfNoSolo = !clip->activeIfNoSolo;

		// If became "active" (in background behind soloing), need to "deactivate" any other Clips - still talking about in the "background" here.
		if (clip->activeIfNoSolo) {
			// For each Clip in session
			for (int c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
				Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);

				if (thisClip != clip && thisClip->output == clip->output) {
					thisClip->activeIfNoSolo = false;
				}
			}
		}
	}

	// Or if no other Clip was soloed...
	else {

		// If Clip STOPPED
		if (!clip->activeIfNoSolo) {

			// If Deluge not playing, easy
			if (!playbackHandler.isEitherClockActive()) {
				clip->activeIfNoSolo = true;

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
				ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

				currentSong->assertActiveness(modelStackWithTimelineCounter);

				// Special case if doing tempoless recording elsewhere - this action stops that
				if (playbackHandler.playbackState) {
					playbackHandler.finishTempolessRecording(true, buttonPressLatency);
					uiNeedsRendering(&sessionView, 0, 0xFFFFFFFF);
					return;
				}
			}

			// Or if Deluge playing
			else {
				userWantsToArmClipsToStartOrSolo(0, clip, false,
				                                 doInstant); // Force "late start" if user holding shift button
			}
		}

		// Or if Clip PLAYING
		else {

			// Playback on
			if (playbackHandler.playbackState) {

				// Tempoless recording
				if (!playbackHandler.isEitherClockActive()) {

					if (clip->getCurrentlyRecordingLinearly()) { // Always true?
						playbackHandler.finishTempolessRecording(true, buttonPressLatency);
						return;
					}
				}

				// Session active
				else if (currentPlaybackMode == this) {

					// Instant-stop
					if (doInstant) {

						if (clip->armState) { // In case also already armed
							clip->armState = ARM_STATE_OFF;
							launchSchedulingMightNeedCancelling();
						}

						// Linear recording - stopping instantly in this case means reducing the Clip's length and arming to stop recording real soon, at next tick
						if (clip->getCurrentlyRecordingLinearly()) {
							cancelAllArming();
							cancelAllLaunchScheduling();
							Action* action = actionLogger.getNewAction(ACTION_RECORD, true);
							currentSong->setClipLength(clip, clip->getLivePos() + 1, action,
							                           false); // Tell it not to resync
							armClipToStopAction(clip);

							sessionView.clipNeedsReRendering(clip);
							if (currentSong->currentClip) uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
						}

						// Or normal
						else {
							clip->expectNoFurtherTicks(currentSong);

							if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
								clip->getClipToRecordTo()->endInstance(playbackHandler.getActualArrangementRecordPos());
							}

							clip->activeIfNoSolo = false;
						}
					}

					// Or normal arm-to-stop
					else {
						armClipToStopAction(clip);
					}
				}

				// Arranger active
				else {
					return;
				}
			}

			// Playback off
			else {
				clip->activeIfNoSolo = false;
			}
		}
	}

	armingChanged();
}

// Beware - calling this might insert a Clip!
void Session::armClipToStopAction(Clip* clip) {
	clip->armState = ARM_STATE_ON_NORMAL;

	int32_t actualCurrentPos = (uint32_t)clip->getClipToRecordTo()->getActualCurrentPosAsIfPlayingInForwardDirection()
	                           % (uint32_t)clip->loopLength;
	int64_t wantToStopAtTime = playbackHandler.getActualSwungTickCount() - actualCurrentPos + clip->loopLength;

	scheduleLaunchTiming(wantToStopAtTime, 1, clip->loopLength);
}

void Session::soloClipAction(Clip* clip, int buttonPressLatency) {
	lastSectionArmed = 255;

	bool anyClipsDeleted = false;

	// If it was already soloed...
	if (clip->soloingInSessionMode) {
		userWantsToUnsoloClip(clip, Buttons::isShiftButtonPressed(), buttonPressLatency);
	}

	// Or if it wasn't...
	else {

		// No automatic overdubs are allowed during soloing, cos that's just too complicated
		anyClipsDeleted = currentSong->deletePendingOverdubs();

		// If either playback is off or there's tempoless recording...
		if (!playbackHandler.isEitherClockActive()) {

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			soloClipRightNow(modelStackWithTimelineCounter);

			// Special case if doing tempoless recording elsewhere
			if (playbackHandler.playbackState) {
				playbackHandler.finishTempolessRecording(true, buttonPressLatency);
				uiNeedsRendering(&sessionView, 0, 0xFFFFFFFF);
				goto renderAndGetOut;
			}
		}

		else {
			userWantsToArmClipsToStartOrSolo(0, clip, false, Buttons::isShiftButtonPressed(), true, 1, true,
			                                 ARM_STATE_ON_TO_SOLO); // Force "late start" if user holding shift button
		}
	}

	armingChanged();

renderAndGetOut:
	if (anyClipsDeleted) uiNeedsRendering(&sessionView);
}

void Session::armSection(uint8_t section, int buttonPressLatency) {

	// Get rid of soloing. And if we're not a "share" section, get rid of arming too
	currentSong->turnSoloingIntoJustPlaying(currentSong->sections[section].numRepetitions != -1);

	// If every Clip in this section is already playing, and no other Clips are (unless we're a "share" section), then there's no need to launch the section because it's already playing.
	// So, make sure this isn't the case before we go and do anything more
	for (int c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

		// If a Clip in the section is not playing...
		if (clip->section == section && !clip->activeIfNoSolo)
			goto yupThatsFine; // Remember, we cancelled any soloing, above

		// If a Clip in another section is playing and we're not a "share" section...
		if (currentSong->sections[section].numRepetitions != -1 && clip->section != section
		    && ((clip->armState != ARM_STATE_OFF) != clip->activeIfNoSolo))
			goto yupThatsFine;
	}

	// If we're here, no need to continue
	launchSchedulingMightNeedCancelling();
	armingChanged();
	return;

yupThatsFine:
	bool stopAllOtherClips = (currentSong->sections[section].numRepetitions >= 0);

	// If Deluge not playing
	if (!playbackHandler.isEitherClockActive()) {

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		armSectionWhenNeitherClockActive(modelStack, section, stopAllOtherClips);

		if (playbackHandler.playbackState) {
			playbackHandler.finishTempolessRecording(true, buttonPressLatency);
			return;
		}
	}

	// Or if Deluge playing
	else {
		userWantsToArmClipsToStartOrSolo(
		    section, NULL, stopAllOtherClips, false,
		    false); // Don't allow "late start". It's too fiddly to implement, and rarely even useful for sections
		lastSectionArmed = section;
	}

	armingChanged();
}

// Probably have to call armingChanged() after this.
// Can sorta be applicable either then !playback state, or when tempoless recording.
void Session::armSectionWhenNeitherClockActive(ModelStack* modelStack, int section, bool stopAllOtherClips) {
	for (int c = 0; c < modelStack->song->sessionClips.getNumElements(); c++) {
		Clip* clip = modelStack->song->sessionClips.getClipAtIndex(c);

		if (clip->section == section && !clip->activeIfNoSolo) {
			clip->activeIfNoSolo = true;

			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			modelStack->song->assertActiveness(modelStackWithTimelineCounter);
		}

		if (stopAllOtherClips && clip->section != section && clip->activeIfNoSolo) {
			//thisClip->expectNoFurtherTicks(currentSong); // No, don't need this, cos it's not playing!
			clip->activeIfNoSolo = false;
		}
	}
}

// Updates LEDs after arming changed
void Session::armingChanged() {
	if (getRootUI() == &sessionView) {
		uiNeedsRendering(&sessionView, 0, 0xFFFFFFFF); // Only need the mute pads
		if (getCurrentUI()->canSeeViewUnderneath()) {
#if HAVE_OLED
			if (!isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)
			    && !isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION))
				renderUIsForOled();
#else
			sessionView.redrawNumericDisplay();
#endif
probablyDoFlashPlayEnable:
			if (hasPlaybackActive()) view.flashPlayEnable();
		}
	}
}

void Session::scheduleOverdubToStartRecording(Clip* overdub, Clip* clipAbove) {

	if (!playbackHandler.isEitherClockActive()) {
		return;
	}

	Clip* waitForClip = clipAbove;
	if (!waitForClip || !currentSong->isClipActive(waitForClip)) {
		waitForClip = currentSong->getLongestActiveClipWithMultipleOrFactorLength(overdub->loopLength, true, overdub);
	}

	uint32_t quantization;
	uint32_t currentPosWithinQuantization;
	uint8_t launchStatus =
	    investigateSyncedLaunch(waitForClip, &currentPosWithinQuantization, &quantization, overdub->loopLength, true);

	// If nothing to sync to, which means no other Clips playing...
	if (launchStatus == LAUNCH_STATUS_NOTHING_TO_SYNC_TO) {
		playbackHandler.endPlayback();
		playbackHandler
		    .setupPlaybackUsingInternalClock(); // We're restarting playback, but it was already happening, so no need for PGMs
	}

	// This case, too, can only actually happen if no Clips are playing
	else if (launchStatus == LAUNCH_STATUS_LAUNCH_ALONG_WITH_EXISTING_LAUNCHING) {}

	else { // LAUNCH_STATUS_LAUNCH_USING_QUANTIZATION
		currentPosWithinQuantization = currentPosWithinQuantization % quantization;
		uint32_t ticksTilStart = quantization - currentPosWithinQuantization;
		int64_t launchTime = playbackHandler.getActualSwungTickCount() + ticksTilStart;

		scheduleLaunchTiming(launchTime, 1, quantization);
	}

	armingChanged();
}

// This can only be called if the Deluge is currently playing
void Session::userWantsToArmClipsToStartOrSolo(uint8_t section, Clip* clip, bool stopAllOtherClips, bool forceLateStart,
                                               bool allowLateStart, int newNumRepeatsTilLaunch,
                                               bool allowSubdividedQuantization, int armState) {

	// Find longest starting Clip length, and what Clip we're waiting on
	uint32_t longestStartingClipLength;
	Clip* waitForClip;

	// ... if launching just a Clip
	if (clip) {

		// Now (Nov 2020), we're going to call getLongestActiveClipWithMultipleOrFactorLength() for all launching of a Clip (not a section). It seems that for the past several
		// years(?) this was broken and would always just wait for the longest Clip.
		waitForClip = currentSong->getLongestActiveClipWithMultipleOrFactorLength(
		    clip->loopLength); // Allow it to return our same Clip if it wants - and if it's active, which could be what we want in the case of arming-to-solo if Clip already active

		longestStartingClipLength = clip->loopLength;
	}

	// ... or if launching a whole section
	else {

		// We don't want to call getLongestActiveClipWithMultipleOrFactorLength(), because when launching a new section, the length of any of the new Clips being launched is
		// irrelevant, cos they won't be playing at the same time as any previously playing ones (normally). Also, this is how it seems to have worked for several years(?)
		// until the bugfix above, and I wouldn't want the behaviour changing on any users.
		waitForClip = currentSong->getLongestClip(false, true);
		longestStartingClipLength = 0;
		for (int c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
			Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);

			if (thisClip->section == section && thisClip->loopLength > longestStartingClipLength) {
				longestStartingClipLength = thisClip->loopLength;
			}
		}
	}

	uint32_t quantization;
	uint32_t currentPosWithinQuantization;
	uint8_t launchStatus = investigateSyncedLaunch(waitForClip, &currentPosWithinQuantization, &quantization,
	                                               longestStartingClipLength, allowSubdividedQuantization);

	// If nothing to sync to, which means no other Clips playing...
	if (launchStatus == LAUNCH_STATUS_NOTHING_TO_SYNC_TO) {

		playbackHandler.endPlayback();

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		// Restart playback.
		// If just one Clip...
		if (clip) {
			clip->activeIfNoSolo = true;
			currentSong->assertActiveness(modelStack->addTimelineCounter(clip));
		}

		// Or, if a whole section...
		else {
			for (int c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
				Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);

				if (thisClip->section == section) {
					thisClip->activeIfNoSolo = true;
					currentSong->assertActiveness(modelStack->addTimelineCounter(thisClip)); // Very inefficient
				}
			}
		}

		playbackHandler
		    .setupPlaybackUsingInternalClock(); // We're restarting playback, but it was already happening, so no need for PGMs
	}

	// This case, too, can only actually happen if no Clips are playing
	else if (launchStatus == LAUNCH_STATUS_LAUNCH_ALONG_WITH_EXISTING_LAUNCHING) {
		// If just one Clip...
		if (clip) {
			armClipLowLevel(clip, armState);
		}

		// Or, if a whole section...
		else {
			for (int l = 0; l < currentSong->sessionClips.getNumElements(); l++) {
				Clip* thisClip = currentSong->sessionClips.getClipAtIndex(l);

				if (thisClip->section == section) {

					// If we're arming a section, we know there's no soloing or armed Clips, so that's easy.
					// Only arm if it's not playing
					if (!thisClip->activeIfNoSolo) {
						armClipLowLevel(thisClip, armState);
					}
				}
			}
		}
	}

	else { // LAUNCH_STATUS_LAUNCH_USING_QUANTIZATION
		armClipsToStartOrSoloWithQuantization(currentPosWithinQuantization, quantization, section, stopAllOtherClips,
		                                      clip, forceLateStart, allowLateStart, newNumRepeatsTilLaunch, armState);
	}
}

int Session::investigateSyncedLaunch(Clip* waitForClip, uint32_t* currentPosWithinQuantization, uint32_t* quantization,
                                     uint32_t longestStartingClipLength, bool allowSubdividedQuantization) {

	// If no Clips are playing...
	if (!waitForClip) {

		// See if any other Clips are armed. We can start at the same time as them
		if (launchEventAtSwungTickCount) return LAUNCH_STATUS_LAUNCH_ALONG_WITH_EXISTING_LAUNCHING;

		// Otherwise...
		else {

			// If a clock is coming in or out, or metronome is on, use that to work out the loop point
			if ((playbackHandler.playbackState & PLAYBACK_CLOCK_EXTERNAL_ACTIVE) || playbackHandler.midiOutClockEnabled
			    || playbackHandler.metronomeOn
			    || cvEngine.gateChannels[WHICH_GATE_OUTPUT_IS_CLOCK].mode == GATE_MODE_SPECIAL
			    || playbackHandler.recording == RECORDING_ARRANGEMENT) {

				uint32_t oneBar = currentSong->getBarLength();

				// If using internal clock (meaning metronome or clock output is on), just quantize to one bar. This is potentially imperfect.
				if (playbackHandler.playbackState & PLAYBACK_CLOCK_INTERNAL_ACTIVE) {
					*quantization = oneBar;
				}

				// Otherwise, quantize to: the sync scale, magnified up to be at least 3 beats long
				else {

					// Work out the length of 3 beats, given the length of 1 bar. Or if 1 bar is already too short, just use that, to avoid bugs
					uint32_t threeBeats;
					if (oneBar >= 2) threeBeats = (oneBar * 3) >> 2;
					else threeBeats = oneBar;

					*quantization = currentSong->getInputTickScale();
					while (*quantization < threeBeats)
						*quantization <<= 1;
				}

				*currentPosWithinQuantization = playbackHandler.getActualSwungTickCount();
				return LAUNCH_STATUS_LAUNCH_USING_QUANTIZATION;
			}

			// Or if using internal clock with no metronome or clock incoming or outgoing, then easy - we can really just restart playback
			else {
				return LAUNCH_STATUS_NOTHING_TO_SYNC_TO;
			}
		}
	}

	// Or, the more normal case where some Clips were already playing
	else {

		// Quantize the launch to the length of the already-playing long Clip, or if the new Clip / section fits into it a whole number of times, use that length
		if (allowSubdividedQuantization && longestStartingClipLength < waitForClip->loopLength
		    && ((uint32_t)waitForClip->loopLength % longestStartingClipLength) == 0) {
			*quantization = longestStartingClipLength;
		}
		else {
			*quantization = waitForClip->loopLength;
		}

		*currentPosWithinQuantization =
		    waitForClip->getClipToRecordTo()->getActualCurrentPosAsIfPlayingInForwardDirection();
		return LAUNCH_STATUS_LAUNCH_USING_QUANTIZATION;
	}
}

// Returns whether we are now armed. If not, it means it's just done the swap already in this function
bool Session::armForSongSwap() {
	Uart::println("Session::armForSongSwap()");

	Clip* waitForClip = currentSong->getLongestClip(false, true);

	uint32_t quantization;
	uint32_t currentPosWithinQuantization;
	uint8_t launchStatus =
	    investigateSyncedLaunch(waitForClip, &currentPosWithinQuantization, &quantization, 0xFFFFFFFF, false);

	// If nothing to sync to, just do the swap right now
	if (launchStatus == LAUNCH_STATUS_NOTHING_TO_SYNC_TO) {
		playbackHandler.doSongSwap();
		playbackHandler.endPlayback();
		playbackHandler.setupPlaybackUsingInternalClock(); // No need to send PGMs - they're sent in doSongSwap().
		return false;
	}

	else if (launchStatus == LAUNCH_STATUS_LAUNCH_USING_QUANTIZATION) {
		int32_t pos = currentPosWithinQuantization % quantization;
		int32_t ticksTilSwap = quantization - pos;
		scheduleLaunchTiming(playbackHandler.getActualSwungTickCount() + ticksTilSwap, 1, quantization);
		Uart::print("ticksTilSwap: ");
		Uart::println(ticksTilSwap);
	}
	else if (launchStatus == LAUNCH_STATUS_LAUNCH_ALONG_WITH_EXISTING_LAUNCHING) {
		// Nothing to do!
	}

	return true;
}

// Returns whether we are now armed. If not, it means it's just done the swap already in this function
bool Session::armForSwitchToArrangement() {

	Clip* waitForClip = currentSong->getLongestClip(false, true);

	uint32_t quantization;
	uint32_t currentPosWithinQuantization;
	uint8_t launchStatus =
	    investigateSyncedLaunch(waitForClip, &currentPosWithinQuantization, &quantization, 2147483647, false);

	if (launchStatus == LAUNCH_STATUS_NOTHING_TO_SYNC_TO) {
		playbackHandler.switchToArrangement();
		return false;
	}

	else if (launchStatus == LAUNCH_STATUS_LAUNCH_USING_QUANTIZATION) {
		int32_t pos = currentPosWithinQuantization % quantization;
		int32_t ticksTilSwap = quantization - pos;
		scheduleLaunchTiming(playbackHandler.getActualSwungTickCount() + ticksTilSwap, 1, quantization);
		switchToArrangementAtLaunchEvent = true;
	}
	else if (launchStatus == LAUNCH_STATUS_LAUNCH_ALONG_WITH_EXISTING_LAUNCHING) {
		switchToArrangementAtLaunchEvent = true;
	}

	return true;
}

void Session::armClipsToStartOrSoloWithQuantization(uint32_t pos, uint32_t quantization, uint8_t section,
                                                    bool stopAllOtherClips, Clip* clip, bool forceLateStart,
                                                    bool allowLateStart, int newNumRepeatsTilLaunch, int armState) {

	// We want to allow the launch point to be a point "within" the longest Clip, at multiple lengths of our shortest launching Clip
	pos = pos % quantization;

	bool doLateStart = forceLateStart;

	// If we were doing this just for one Clip (so a late-start might be allowed too)...
	if (clip) {
		if (!doLateStart
		    && allowLateStart) { // Reminder - late start is never allowed for sections - just cos it's not that useful, and tricky to implement

			// See if that given point was only just reached a few milliseconds ago - in which case we'll do a "late start"
			uint32_t timeAgo = pos * playbackHandler.getTimePerInternalTick(); // Accurate enough
			doLateStart = (timeAgo < noteOnLatenessAllowed);
		}

		armClipToStartOrSoloUsingQuantization(clip, doLateStart, pos, armState);
	}

	// Or, if we were doing it for a whole section - which means that we know armState == ARM_STATE_ON_NORMAL, and no late-start
	else {
		OpenAddressingHashTableWith32bitKey outputsWeHavePickedAClipFor;

		// Ok, we're going to do a big complex thing where we traverse just once (or occasionally twice) through all sessionClips.
		// Reverse order so behaviour of this new code is the same as the old code
		for (int c = currentSong->sessionClips.getNumElements() - 1; c >= 0; c--) {
			Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);

			// If thisClip is in the section we're wanting to arm...
			if (thisClip->section == section) {

				// Because we're arming a section, we know there's no soloing Clips, so that's easy.

				Output* output = thisClip->output;

				bool alreadyPickedAClip = false;
				outputsWeHavePickedAClipFor.insert((uint32_t)output, &alreadyPickedAClip);

				// If we've already picked a Clip for this same Output...
				if (alreadyPickedAClip) {

					if (!output->nextClipFoundShouldGetArmed) {
						if (thisClip->activeIfNoSolo) output->nextClipFoundShouldGetArmed = true;
						goto weWantThisClipInactive;
					}
					else {

						// We're gonna make this Clip active, but we may have already tried to make a previous
						// one on this Output active, so go back through all previous ones and deactivate / disarm them
						for (int d = currentSong->sessionClips.getNumElements() - 1; d > c; d--) {
							Clip* thatClip = currentSong->sessionClips.getClipAtIndex(d);
							if (thatClip->output == output) {
								thatClip->armState = thatClip->activeIfNoSolo ? ARM_STATE_ON_NORMAL : ARM_STATE_OFF;
							}
						}

						output->nextClipFoundShouldGetArmed = false;
						goto wantActive;
					}
				}

				// Or if haven't yet picked a Clip for this Output...
				else {
wantActive:
					// If it's already active (less common)...
					if (thisClip->activeIfNoSolo) {
						// If it's armed to stop, cancel that
						if (thisClip->armState) {
							thisClip->armState = ARM_STATE_OFF;
						}
						output->nextClipFoundShouldGetArmed = true;
					}

					// Or if it's inactive (the most normal case), we want to arm it to launch.
					else {
						thisClip->armState = armState;
						output->nextClipFoundShouldGetArmed = false;
					}
				}
			}

			// Or if thisClip is in a different section, and if we're going to stop all such Clips...
			else {

				// If we definitely want to stop it because of its section...
				if (stopAllOtherClips) {

weWantThisClipInactive:
					// If it's active, arm it to stop
					if (thisClip->activeIfNoSolo) {
						thisClip->armState = ARM_STATE_ON_NORMAL;
					}

					// Or if it's already inactive...
					else {
						// If it's armed to start, cancel that
						if (thisClip->armState) {
							thisClip->armState = ARM_STATE_OFF;
						}
					}
				}

				// Or, if we don't want to stop it because of its section, we'll need to make sure that it just doesn't share an Output with one of the
				// clips which is gonna get launched, in our new section. Unfortunately, we haven't seen all of those yet, so we'll have to come through
				// and do this in a separate, second traversal - see next comment below
			}
		}

		// Ok, and as mentioned in the big comment directly above, if we're not doing a stopAllOtherClips, only now are we in a position to know
		// which of those other Clips we still will need to stop because of their Output
		if (!stopAllOtherClips) {

			for (int c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
				Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);

				// Ok, so if it's from another section (only those, because we've already dealt with the ones in our section)...
				if (thisClip->section != section) {

					// And if it's currently active...
					if (thisClip->activeIfNoSolo) {

						// If we've already picked a Clip for this same Output, we definitely don't want this one remaining active, so arm it to stop
						if (outputsWeHavePickedAClipFor.lookup((uint32_t)thisClip->output)) {
							thisClip->armState = ARM_STATE_ON_NORMAL;
						}
					}
				}
			}
		}
	}

	if (!doLateStart) {
		uint32_t ticksTilStart = quantization - pos;
		int64_t launchTime = playbackHandler.getActualSwungTickCount() + ticksTilStart;

		scheduleLaunchTiming(launchTime, newNumRepeatsTilLaunch, quantization);
	}
}

// (I'm fairly sure) this shouldn't be / isn't called if the Clip is soloing
void Session::armClipToStartOrSoloUsingQuantization(Clip* thisClip, bool doLateStart, uint32_t pos, int armState,
                                                    bool mustUnarmOtherClipsWithSameOutput) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, thisClip);

	// Arm to start soloing
	if (armState == ARM_STATE_ON_TO_SOLO) {
		// If need to enact change instantly...
		if (doLateStart) {

			// If that was already armed, un-arm it
			if (thisClip->armState) {
				thisClip->armState = ARM_STATE_OFF;
				launchSchedulingMightNeedCancelling();
			}

			//currentSong->deactivateAnyArrangementOnlyClips(); // In case any left playing after switch from arrangement

			bool wasAlreadyActive = currentSong->isClipActive(thisClip);

			soloClipRightNow(modelStack);

			if (!wasAlreadyActive) {
				goto setPosAndStuff;
			}
		}

		// Otherwise, arm it
		else {
			armClipLowLevel(thisClip, ARM_STATE_ON_TO_SOLO);
		}
	}

	// Arm to start regular play
	else {

		// If late start...
		if (doLateStart) {

			if (thisClip->armState) { // In case also already armed
				thisClip->armState = ARM_STATE_OFF;
				launchSchedulingMightNeedCancelling();
			}

			thisClip->activeIfNoSolo = true;

			// Must call this before setPos, because that does stuff with ParamManagers
			currentSong->assertActiveness(modelStack, playbackHandler.getActualArrangementRecordPos() - pos);

setPosAndStuff:
			// pos is a "live" pos, so we have to subtract swungTicksSkipped before setting the Clip's lastProcessedPos, because it's soon going to be jumped forward by that many ticks
			int32_t modifiedStartPos = (int32_t)pos - playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick();
			thisClip->setPos(modelStack, modifiedStartPos);

			thisClip->resumePlayback(modelStack);

			// If recording session to arranger, do that
			if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
				thisClip->beginInstance(currentSong, playbackHandler.getActualArrangementRecordPos() - pos);
			}
		}

		// Or if normal start...
		else {
			armClipLowLevel(thisClip, ARM_STATE_ON_NORMAL, mustUnarmOtherClipsWithSameOutput);
		}
	}
}

void Session::cancelAllArming() {
	for (int l = 0; l < currentSong->sessionClips.getNumElements(); l++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(l);
		clip->cancelAnyArming();
	}
}

void Session::armClipLowLevel(Clip* clipToArm, int armState, bool mustUnarmOtherClipsWithSameOutput) {

	clipToArm->armState = armState;

	// Unarm any armed Clips with same Output, if we're doing that
	if (mustUnarmOtherClipsWithSameOutput) {
		// All session Clips
		for (int c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

			if (clip != clipToArm && !clip->soloingInSessionMode && !clip->activeIfNoSolo && clip->armState
			    && clip->output == clipToArm->output) {
				clip->armState = ARM_STATE_OFF;
			}
		}
	}
}

int Session::userWantsToArmNextSection(int numRepetitions) {

	int currentSection = getCurrentSection();
	if (currentSection < 254) {
		if (numRepetitions == -1) numRepetitions = currentSong->sections[currentSection].numRepetitions;

		if (numRepetitions >= 1) {
			armNextSection(currentSection, numRepetitions);
			armingChanged();
		}
	}
	return currentSection;
}

// Only returns result if all Clips in section are playing, and no others
// Exactly what the return values of 255 and 254 mean has been lost, but they're treated as interchangeable by the function that calls this anyway
int Session::getCurrentSection() {

	if (currentSong->getAnyClipsSoloing()) return 255;

	int section = 255;

	bool anyUnlaunchedLoopablesInSection[MAX_NUM_SECTIONS];
	memset(anyUnlaunchedLoopablesInSection, 0, sizeof(anyUnlaunchedLoopablesInSection));

	for (int l = 0; l < currentSong->sessionClips.getNumElements(); l++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(l);

		if (clip->activeIfNoSolo) {
			if (section == 255) section = clip->section;
			else if (section != clip->section) return 254;
		}
		else {
			if (ALPHA_OR_BETA_VERSION && clip->section > MAX_NUM_SECTIONS) numericDriver.freezeWithError("E243");
			anyUnlaunchedLoopablesInSection[clip->section] = true;
		}
	}

	if (anyUnlaunchedLoopablesInSection[section]) return 255;
	return section;
}

bool Session::areAnyClipsArmed() {
	for (int l = 0; l < currentSong->sessionClips.getNumElements(); l++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(l);

		if (clip->armState) return true;
	}

	return false;
}

// This is a little bit un-ideal, but after an undo or redo, this will be called, and it will tell every active Clip
// to potentially expect a note or automation event - and to re-get all current automation values.
// I wish we could easily just do this to the Clips that need it, but we don't store an easy list of just the Clips affected by each Action.
// This is only to be called if playbackHandler.isEitherClockActive().
void Session::reversionDone() {

	// For each Clip in session and arranger
	ClipArray* clipArray = &currentSong->sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);

		if (currentSong->isClipActive(clip)) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
			    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);

			clip->reGetParameterAutomation(modelStackWithTimelineCounter);
			clip->expectEvent();
		}
	}
	if (clipArray != &currentSong->arrangementOnlyClips) {
		clipArray = &currentSong->arrangementOnlyClips;
		goto traverseClips;
	}
}

// PlaybackMode implementation -----------------------------------------------------------------------

void Session::setupPlayback() {

	currentSong->setParamsInAutomationMode(playbackHandler.recording == RECORDING_ARRANGEMENT);

	lastSectionArmed = 255;
}

// Returns whether to do an instant song swap
bool Session::endPlayback() {
	lastSectionArmed = 255;

	bool anyClipsRemoved = currentSong->deletePendingOverdubs();

	for (int c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

		clip->cancelAnyArming();
		if (currentSong->isClipActive(clip)) {
			clip->expectNoFurtherTicks(currentSong);
		}
	}

	currentSong->deactivateAnyArrangementOnlyClips(); // In case any still playing after switch from arrangement

	// If we were waiting for a launch event, we've now stopped so will never reach that point in time, so we'd better swap right now.
	if (launchEventAtSwungTickCount || currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_UNARMED) {
		cancelAllLaunchScheduling();

		if (preLoadedSong) {
			return true;
		}
		else {
			armingChanged();
		}
	}

	// If pending overdubs deleted...
	if (anyClipsRemoved) {

		// Re-render
		uiNeedsRendering(&sessionView);

		// And exit RECORD mode, as indicated on LED
		if (playbackHandler.recording == RECORDING_NORMAL) {
			playbackHandler.recording = RECORDING_OFF;
			// I guess we're gonna update the LED states sometime soon...
		}
	}
	return false; // No song swap
}

bool Session::wantsToDoTempolessRecord(int32_t newPos) {

	bool mightDoTempolessRecord =
	    (!newPos && playbackHandler.recording == RECORDING_NORMAL && !playbackHandler.metronomeOn);
	if (!mightDoTempolessRecord) return false;

	bool anyActiveClips = false;

	for (int c = currentSong->sessionClips.getNumElements() - 1; c >= 0; c--) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

		if (currentSong->isClipActive(clip)) {
			anyActiveClips = true;

			if (clip->type != CLIP_TYPE_AUDIO) return false; // Cos there's a non-audio clip playing or recording

			if (!clip->wantsToBeginLinearRecording(currentSong)) return false;
		}
	}

	if (!anyActiveClips) return false;

	return true;
}

void Session::resetPlayPos(int32_t newPos, bool doingComplete, int buttonPressLatency) {

	// This function may begin a tempoless record - but it doesn't actually know or need to know whether that's the resulting outcome

	AudioEngine::bypassCulling = true;

	currentSong
	    ->deactivateAnyArrangementOnlyClips(); // In case any still playing after switch from arrangement. Remember, this function will be called on playback begin, song swap, and more

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStack);

		currentSong->paramManager.setPlayPos(arrangement.playbackStartedAtPos, modelStackWithThreeMainThings, false);
	}

	int32_t distanceTilLaunchEvent = 0;

	for (int c = currentSong->sessionClips.getNumElements() - 1; c >= 0; c--) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

		// Sometimes, after finishing a tempoless record, a new pending overdub will have been created, and we need to act on it here
		if (clip->isPendingOverdub) {
			clip->activeIfNoSolo = true;
			clip->armState = ARM_STATE_OFF;
			goto yeahNahItsOn;
		}

		// If Clip active, or if it's a pending overdub, which means we wanna make it active...
		if (currentSong->isClipActive(clip)) {

yeahNahItsOn:
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			clip->setPos(modelStackWithTimelineCounter, newPos, true);

			if (doingComplete) {
				// If starting after 0, must do the "resume" function, to get samples playing from right point, etc
				if (newPos) {
					clip->resumePlayback(modelStackWithTimelineCounter);
				}

				// Otherwise, so long as not doing count-in, begin linear recording
				else {

					giveClipOpportunityToBeginLinearRecording(clip, c, buttonPressLatency);

					if (clip->armState
					    == ARM_STATE_ON_NORMAL) { // What's this for again? Auto arming of sections? I think not linear recording...
						distanceTilLaunchEvent = getMax(distanceTilLaunchEvent, clip->loopLength);
					}
				}
			}

			clip->output->setActiveClip(
			    modelStackWithTimelineCounter); // Not sure quite why we needed to set this here?
		}
	}

	if (doingComplete) {
		if (!playbackHandler.isEitherClockActive()) { // If tempoless recording...
			cancelAllArming();
		}

		// If just became armed (audio clip began recording)... The placement of this probably isn't quite ideal...
		else if (distanceTilLaunchEvent) {
			scheduleLaunchTiming(playbackHandler.lastSwungTickActioned + distanceTilLaunchEvent, 1,
			                     distanceTilLaunchEvent);
			armingChanged(); // This isn't really ideal. Is here for AudioClips which just armed themselves in setPos() call
		}
	}
}

// TODO: I'd like to have it so this doesn't get called on the first tick of playback - now that this function is also responsible for doing the incrementing.
// It works fine because we supply the increment as 0 in that case, but it'd be more meaningful this proposed new way...

// Returns whether Song was swapped
bool Session::considerLaunchEvent(int32_t numTicksBeingIncremented) {

	bool swappedSong = false;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	// We now increment the currentPos of all Clips before doing any launch event - so that any new Clips which get launched won't then get their pos incremented

	// For each Clip in session and arranger (we include arrangement-only Clips, which might still be left playing after switching from arrangement to session)
	ClipArray* clipArray = &currentSong->sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);

		if (!currentSong->isClipActive(clip)) continue;

		if (clip->output->activeClip && clip->output->activeClip->beingRecordedFromClip == clip) {
			clip = clip->output->activeClip;
		}

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

		clip->incrementPos(modelStackWithTimelineCounter, numTicksBeingIncremented);
	}
	if (clipArray != &currentSong->arrangementOnlyClips) {
		clipArray = &currentSong->arrangementOnlyClips;
		goto traverseClips;
	}

	bool enforceSettingUpArming = false;

	// If launch event right now
	if (launchEventAtSwungTickCount && playbackHandler.lastSwungTickActioned >= launchEventAtSwungTickCount) {

		numRepeatsTilLaunch--;

		// If no more repeats remain, do the actual launch event now!
		if (numRepeatsTilLaunch <= 0) {

			if (playbackHandler.stopOutputRecordingAtLoopEnd && audioRecorder.isCurrentlyResampling()) {
				audioRecorder.endRecordingSoon();
			}

			// If we're doing a song swap...
			if (preLoadedSong) {
				cancelAllLaunchScheduling();
				lastSectionArmed = 255;
				playbackHandler.doSongSwap();
				swappedSong = true;

				// If new song has us in arrangement...
				if (currentPlaybackMode == &arrangement) {
					return swappedSong;
				}

				currentPlaybackMode->resetPlayPos(
				    0); // activeClips have been set up already / PGMs have been sent. Calling this will resync arpeggiators though...
				// If switching to arranger, that'll happen as part of doSongSwap(), above

				enforceSettingUpArming = true;
			}

			// Or if switching to arrangement...
			else if (switchToArrangementAtLaunchEvent) {
				playbackHandler.switchToArrangement();
				return swappedSong;
			}

			// Or if Clips launching...
			else {

				// NOTE: we do NOT want to set playbackHandler.swungTicksSkipped to 0 here, cos that'd mess up all the other Clips!

				cancelAllLaunchScheduling();
				doLaunch();
				armingChanged();

				// If playback was caused to end as part of that whole process, get out
				if (!playbackHandler.isEitherClockActive()) {
					return swappedSong;
				}
			}
		}

		// Or if repeats do remain, just go onto the next one
		else {
			launchEventAtSwungTickCount = playbackHandler.lastSwungTickActioned + currentArmedLaunchLengthForOneRepeat;
#if HAVE_OLED
			if (getCurrentUI() == &loadSongUI) loadSongUI.displayLoopsRemainingPopup();
			else if (getRootUI() == &sessionView && !isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW))
				renderUIsForOled();
#else
			sessionView.redrawNumericDisplay();
#endif
		}
	}

	// If this is the first tick, we have to do some stuff to arm the first song-section change
	if (playbackHandler.lastSwungTickActioned == 0 || enforceSettingUpArming) {
		int currentSection = userWantsToArmNextSection();

		if (currentSection < 254 && currentSong->areAllClipsInSectionPlaying(currentSection)) {
			currentSong->sectionToReturnToAfterSongEnd = currentSection;
		}
		else {
			currentSong->sectionToReturnToAfterSongEnd = 255;
		}
	}

	return swappedSong;
}

void Session::doTickForward(int posIncrement) {

	if (launchEventAtSwungTickCount) {
		int32_t ticksTilLaunchEvent = launchEventAtSwungTickCount - playbackHandler.lastSwungTickActioned;

		playbackHandler.swungTicksTilNextEvent = getMin(ticksTilLaunchEvent, playbackHandler.swungTicksTilNextEvent);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings = currentSong->addToModelStack(modelStack);

		if (currentSong->paramManager.mightContainAutomation()) {
			currentSong->paramManager.processCurrentPos(modelStackWithThreeMainThings, posIncrement, false);
			playbackHandler.swungTicksTilNextEvent =
			    getMin(playbackHandler.swungTicksTilNextEvent, currentSong->paramManager.ticksTilNextEvent);
		}
	}

	// Tell all the Clips that it's tick time. Including arrangement-only Clips, which might still be left playing after switching from arrangement to session
	// For each Clip in session and arranger
	ClipArray* clipArray = &currentSong->sessionClips;
traverseClips:
	for (int c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);

		if (!currentSong->isClipActive(clip)) continue;

		if (clip->output->activeClip && clip->output->activeClip->beingRecordedFromClip == clip) {
			clip = clip->output->activeClip;
		}

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

		// No need to do the actual incrementing - that's been done for all Clips (except ones which have only just launched), up in considerLaunchEvent()

		clip->processCurrentPos(modelStackWithTimelineCounter,
		                        posIncrement); // May create new Clip and put it in the ModelStack - we'll check below.

		// NOTE: posIncrement is the number of ticks which we incremented by in considerLaunchEvent(). But for Clips which were only just launched in there,
		// well the won't have been incremented, so it would be more correct if posIncrement were 0 here. But, I don't believe there's any ill-effect from having a
		// posIncrement too big in this case. It's just not super elegant.

		// New Clip may have been returned for AudioClips being recorded from session to arranger
		if (modelStackWithTimelineCounter->getTimelineCounter() != clip) {
			Clip* newClip = (Clip*)modelStackWithTimelineCounter->getTimelineCounter();
			newClip->processCurrentPos(modelStackWithTimelineCounter, 0);

			if (view.activeModControllableModelStack.getTimelineCounterAllowNull() == clip) {
				view.activeModControllableModelStack.setTimelineCounter(newClip);
				view.activeModControllableModelStack.paramManager = &newClip->paramManager;
			}
		}
	}
	if (clipArray != &currentSong->arrangementOnlyClips) {
		clipArray = &currentSong->arrangementOnlyClips;
		goto traverseClips;
	}

	// Do arps too (hmmm, could we want to do this in considerLaunchEvent() instead, just like the incrementing?)
	for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {

		int32_t posForArp;
		if (thisOutput->activeClip && currentSong->isClipActive(thisOutput->activeClip))
			posForArp = thisOutput->activeClip->lastProcessedPos;
		else posForArp = playbackHandler.lastSwungTickActioned;

		int32_t ticksTilNextArpEvent = thisOutput->doTickForwardForArp(modelStack, posForArp);
		playbackHandler.swungTicksTilNextEvent = getMin(ticksTilNextArpEvent, playbackHandler.swungTicksTilNextEvent);
	}

	/*
    for (Instrument* thisInstrument = currentSong->firstInstrument; thisInstrument; thisInstrument = thisInstrument->next) {
    	if (thisInstrument->activeClip && isClipActive(thisInstrument->activeClip)) {
    		thisInstrument->activeClip->doTickForward(posIncrement);
    	}
    }
    */
}

void Session::resyncToSongTicks(Song* song) {

	for (int c = 0; c < song->sessionClips.getNumElements(); c++) {
		Clip* clip = song->sessionClips.getClipAtIndex(c);

		if (song->isClipActive(clip)) {
			reSyncClipToSongTicks(clip);
		}
	}
}

void Session::unsoloClip(Clip* clip) {
	clip->soloingInSessionMode = false;

	currentSong->reassessWhetherAnyClipsSoloing();

	if (!hasPlaybackActive()) return;

	bool anyClipsStillSoloing = currentSong->getAnyClipsSoloing();

	// If any other Clips are still soloing, or this Clip isn't active outside of solo mode, we need to shut that Clip up
	if (anyClipsStillSoloing || !clip->activeIfNoSolo) {
		clip->expectNoFurtherTicks(currentSong);

		if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
			clip->getClipToRecordTo()->endInstance(playbackHandler.getActualArrangementRecordPos());
		}
	}

	// Re-activate *other* Clips (i.e. not this one) if this was the only soloed Clip
	if (!anyClipsStillSoloing) {

		int32_t modifiedStartPos = (int32_t)clip->lastProcessedPos; // - swungTicksSkipped;
		if (modifiedStartPos < 0) modifiedStartPos += clip->loopLength;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		for (int c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
			Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);

			if (thisClip != clip && thisClip->activeIfNoSolo) {

				ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(thisClip);

				// Must call this before setPos, because that does stuff with ParamManagers
				currentSong->assertActiveness(modelStackWithTimelineCounter);

				if (hasPlaybackActive()) {
					thisClip->setPos(modelStackWithTimelineCounter,
					                 (uint32_t)modifiedStartPos % (uint32_t)thisClip->loopLength);
					thisClip->resumePlayback(modelStackWithTimelineCounter);
				}
			}
		}
	}
}

void Session::soloClipRightNow(ModelStackWithTimelineCounter* modelStack) {
	Clip* clip = (Clip*)modelStack->getTimelineCounter();

	bool anyOthersAlreadySoloed = modelStack->song->getAnyClipsSoloing();

	modelStack->song->anyClipsSoloing = true;
	clip->soloingInSessionMode = true;

	// If no other Clips were soloed yet
	if (!anyOthersAlreadySoloed) {

		bool cancelledAnyArming = false;

		if (playbackHandler.isEitherClockActive()) {

			// Need to deactivate all other Clips
			// - *and* also cancel any other arming, unless it's arming to become soloed.
			// Non-solo-related arming is not allowed when Clips are soloing.
			for (int c = 0; c < modelStack->song->sessionClips.getNumElements(); c++) {
				Clip* thisClip = modelStack->song->sessionClips.getClipAtIndex(c);
				if (thisClip != clip) { // Only *other* Clips
					if (thisClip->activeIfNoSolo) {
						thisClip->expectNoFurtherTicks(modelStack->song);

						if (playbackHandler.recording == RECORDING_ARRANGEMENT) {
							thisClip->getClipToRecordTo()->endInstance(playbackHandler.getActualArrangementRecordPos());
						}
					}

					// As noted above, non-solo arming is not allowed now that there will be a Clip soloing.
					if (thisClip->armState == ARM_STATE_ON_NORMAL) {
						thisClip->armState = ARM_STATE_OFF;
						cancelledAnyArming = true;
					}
				}
			}
		}

		// If we cancelled any arming, we need to finish that off
		if (cancelledAnyArming) {
			launchSchedulingMightNeedCancelling();
		}

		// Might need to activate this Clip
		if (!clip->activeIfNoSolo) {
			goto doAssertThisClip;
		}
	}

	// Or if other Clips were soloed...
	else {
doAssertThisClip:
		modelStack->song->assertActiveness(modelStack);
		// pos will get set by the caller if necessary
	}
}

bool Session::isOutputAvailable(Output* output) {
	if (!playbackHandler.playbackState || !output->activeClip) return true;

	return !currentSong->doesOutputHaveActiveClipInSession(output);
}

void Session::stopOutputRecordingAtLoopEnd() {
	// If no launch-event currently, plan one
	if (!launchEventAtSwungTickCount) {
		armAllClipsToStop(1);
		lastSectionArmed = 254;
		armingChanged();
	}

	playbackHandler.stopOutputRecordingAtLoopEnd = true;
}

int32_t Session::getPosAtWhichClipWillCut(ModelStackWithTimelineCounter const* modelStack) {

	Clip* clip = (Clip*)modelStack->getTimelineCounter();

	// If recording arrangement, pretend it's gonna cut at the end of the current length, cos we're actually going to auto-extend it when we get
	// there, so we don't want any wrapping-around happening
	if (clip->isArrangementOnlyClip() && playbackHandler.recording == RECORDING_ARRANGEMENT
	    && clip->beingRecordedFromClip) {
		return clip->currentlyPlayingReversed ? 0 : clip->loopLength;
	}

	int32_t cutPos;

	if (willClipContinuePlayingAtEnd(modelStack)) {
		cutPos = clip->currentlyPlayingReversed ? (-2147483648) : 2147483647; // If it's gonna loop, it's not gonna cut
	}

	else {
		int32_t ticksTilLaunchEvent = launchEventAtSwungTickCount - playbackHandler.lastSwungTickActioned;
		if (clip->currentlyPlayingReversed) ticksTilLaunchEvent = -ticksTilLaunchEvent;
		cutPos = clip->lastProcessedPos + ticksTilLaunchEvent;
	}

	// If pingponging, that's actually going to get referred to as a cut.
	if (clip->sequenceDirectionMode == SEQUENCE_DIRECTION_PINGPONG) {
		if (clip->currentlyPlayingReversed) {
			if (cutPos < 0) {
				cutPos =
				    clip->lastProcessedPos
				        ? 0
				        : -clip->loopLength; // Check we're not right at pos 0, as we briefly will be when we pingpong at the right-hand end of the Clip/etc.
			}
		}
		else {
			if (cutPos > clip->loopLength) cutPos = clip->loopLength;
		}
	}

	return cutPos;
}

bool Session::willClipContinuePlayingAtEnd(ModelStackWithTimelineCounter const* modelStack) {

	Clip* clip = (Clip*)modelStack->getTimelineCounter();

	if (!modelStack->song->isClipActive(clip))
		return false; // If Clip not active, just say it won't loop. We need that, cos an AudioClip's Sample may
		              // keep playing just after its Clip has stopped, and we don't wanna think it needs to loop

	// Note: this isn't quite perfect - it doesn’t know if Clip will cut out due to another one launching. But the ill effects of this are pretty minor.
	bool willLoop =
	    !launchEventAtSwungTickCount             // If no launch event scheduled, obviously it'll loop
	    || numRepeatsTilLaunch > 1               // If the launch event is gonna just trigger another repeat, it'll loop
	    || clip->armState != ARM_STATE_ON_NORMAL // If not armed, or armed to solo, it'll loop (except see above)
	    || (clip->soloingInSessionMode
	        && clip->activeIfNoSolo); // We know from the previous test that clip is armed. If it's soloing, that means it's armed to stop soloing. And if it is activeIfNoSolo, that means it'll keep playing, if we assume *all* clips are going to stop soloing (a false positive here doesn't matter too much)

	// Ok, that's most of our tests done. If one of them gave a true, we can get out now.
	if (willLoop) return true;

	// Otherwise, one final test, which needed a bit of pre-logic.
	int32_t ticksTilReachLoopPoint =
	    clip->currentlyPlayingReversed ? clip->lastProcessedPos : (clip->loopLength - clip->lastProcessedPos);
	return (launchEventAtSwungTickCount - playbackHandler.lastSwungTickActioned
	        > ticksTilReachLoopPoint); // If launch event is after loop-point, it'll loop.
}

// TODO: should this now actually check that it's not pingponging?
bool Session::willClipLoopAtSomePoint(ModelStackWithTimelineCounter const* modelStack) {
	return willClipContinuePlayingAtEnd(modelStack);
}

// The point of this is to re-enable any other Clip with same Output
bool Session::deletingClipWhichCouldBeAbandonedOverdub(Clip* clip) {

	bool shouldBeActiveWhileExistent =
	    clip->activeIfNoSolo; // Yep, this works better (in some complex scenarios I tested) than calling isClipActive(), which would take soloing into account

	if (shouldBeActiveWhileExistent && !(playbackHandler.playbackState && currentPlaybackMode == &arrangement)) {
		int newClipIndex;
		Clip* newClip = currentSong->getSessionClipWithOutput(clip->output, -1, clip, &newClipIndex, true);
		if (newClip) {
			toggleClipStatus(newClip, &newClipIndex, true, 0);
		}
	}

	return shouldBeActiveWhileExistent;
}
