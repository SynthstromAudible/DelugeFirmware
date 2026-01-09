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

#include "playback/mode/session.h"
#include "definitions_cxx.hpp"
#include "gui/ui/audio_recorder.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "io/debug/log.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/clip/sequencer/sequencer_mode.h"
#include "model/drum/drum.h"
#include "model/note/note_row.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/sound/sound_instrument.h"
#include "processing/stem_export/stem_export.h"
#include <string.h>
// #include <algorithm>
#include "gui/ui/load/load_song_ui.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "model/clip/audio_clip.h"
#include "model/model_stack.h"
#include "playback/mode/arrangement.h"
#include "storage/storage_manager.h"
#include "util/container/hashtable/open_addressing_hash_table.h"

Session session{};

enum class LaunchStatus {
	NOTHING_TO_SYNC_TO,
	LAUNCH_USING_QUANTIZATION,
	LAUNCH_ALONG_WITH_EXISTING_LAUNCHING,
};

using namespace deluge::gui::colours;
const Colour defaultClipSectionColours[] = {RGB::fromHue(102), // bright light blue
                                            RGB::fromHue(168), // bright dark pink
                                            RGB::fromHue(24),  // bright light orange
                                            RGB::fromHue(84),  // bright turquoise
                                            red,
                                            lime,
                                            blue,
                                            RGB::fromHue(12),  // bright dark orange
                                            RGB::fromHue(147), // bright purple
                                            yellow,
                                            green,
                                            RGB::fromHue(157), // bright magenta
                                            pastel::blue,
                                            pink_full,
                                            pastel::orange,
                                            pastel::green,
                                            pink.forTail(),
                                            lime.forTail(),
                                            cyan.forTail(),
                                            orange.forTail(),
                                            purple.forTail(),
                                            pastel::yellow.forTail(),
                                            green.forTail(),
                                            magenta.forTail()};

Session::Session() {
	cancelAllLaunchScheduling();
	lastSectionArmed = REALLY_OUT_OF_RANGE;
}

void Session::armAllClipsToStop(int32_t afterNumRepeats) {

	Clip* waitForClip = currentSong->getLongestClip(false, true);

	if (!waitForClip) {
		return; // Nothing to do if no Clips are playing
	}

	uint32_t quantization;
	uint32_t currentPosWithinQuantization;
	LaunchStatus launchStatus =
	    investigateSyncedLaunch(waitForClip, &currentPosWithinQuantization, &quantization, 0xFFFFFFFF, false);

	if (launchStatus == LaunchStatus::NOTHING_TO_SYNC_TO) {
		// We'd never actually get here, because there always are Clips playing if this function gets called I think
	}

	else if (launchStatus == LaunchStatus::LAUNCH_USING_QUANTIZATION) {
		int32_t pos = currentPosWithinQuantization % quantization;
		int32_t ticksTilSwap = quantization - pos;
		scheduleLaunchTiming(playbackHandler.getActualSwungTickCount() + ticksTilSwap, afterNumRepeats, quantization);
	}

	else if (launchStatus == LaunchStatus::LAUNCH_ALONG_WITH_EXISTING_LAUNCHING) {
		// Nothing to do!
	}

	// If any soloing Clips
	if (currentSong->getAnyClipsSoloing()) {
		for (int32_t l = 0; l < currentSong->sessionClips.getNumElements(); l++) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(l);
			clip->activeIfNoSolo = false;
			if (clip->soloingInSessionMode) {
				clip->armState = ArmState::ON_NORMAL;
			}
		}
	}

	// Or if no soloing Clips
	else {
		for (int32_t l = 0; l < currentSong->sessionClips.getNumElements(); l++) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(l);
			clip->armState = clip->activeIfNoSolo ? ArmState::ON_NORMAL : ArmState::OFF;
		}
	}
}

void Session::armNextSection(int32_t oldSection, int32_t numRepetitions) {
	if (numRepetitions <= -1) {
		numRepetitions = currentSong->sections[oldSection].numRepetitions;
	}
	if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeRows) {
		if (currentSong->sessionClips.getClipAtIndex(0)->section != oldSection) {

			int32_t newSection = SECTION_OUT_OF_RANGE;
			for (int32_t c = 1; c < currentSong->sessionClips.getNumElements(); c++) { // NOTE: starts at 1, not 0
				Clip* clip = currentSong->sessionClips.getClipAtIndex(c);
				int32_t tempSection = currentSong->sessionClips.getClipAtIndex(c - 1)->section;
				if (currentSong->sections[tempSection].numRepetitions != LAUNCH_EXCLUSIVE) {
					newSection = tempSection;
				}
				if (clip->section == oldSection && newSection != SECTION_OUT_OF_RANGE) {
					userWantsToArmClipsToStartOrSolo(newSection, nullptr, true, false, false, numRepetitions, false);
					lastSectionArmed = newSection;
					return;
				}
			}
		}
	}
	// grid mode - just go to the next section, no need to worry about what order they're in
	else if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeGrid) {
		if (oldSection < kMaxNumSections) {
			int32_t newSection = SECTION_OUT_OF_RANGE;
			for (int32_t c = oldSection + 1; c <= kMaxNumSections; ++c) {
				if (currentSong->sections[c].numRepetitions != LAUNCH_EXCLUSIVE) {
					newSection = c;
					break;
				}
			}
			if (newSection != SECTION_OUT_OF_RANGE) {
				userWantsToArmClipsToStartOrSolo(newSection, nullptr, true, false, false, numRepetitions, false);
				lastSectionArmed = newSection;
			}
			return;
		}
	}

	// If we're here, that was the last section
	armAllClipsToStop(numRepetitions);
	lastSectionArmed = SECTION_OUT_OF_RANGE;
}

// Returns whether it began
bool Session::giveClipOpportunityToBeginLinearRecording(Clip* clip, int32_t clipIndex, int32_t buttonPressLatency) {

	if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
		return false; // Not allowed if recording to arranger
	}

	// Must do this before calling opportunityToBeginLinearLoopRecording(), which may clone a new Output
	bool currentClipHasSameOutput = (getCurrentClip() && getCurrentOutput() == clip->output);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);

	bool newOutputCreated;
	// May create new Output
	bool begun = clip->opportunityToBeginSessionLinearRecording(modelStack, &newOutputCreated, buttonPressLatency);

	if (begun) {

		if (getRootUI() == &sessionView) {
			sessionView.clipNeedsReRendering(clip); // Necessary for InstrumentClips
		}

		// if we're creating a new recording based on a previous clip on the same
		// output, set current clip to the new one
		else if (currentClipHasSameOutput && getCurrentUI()->toClipMinder()) {

			currentSong->setCurrentClip(clip);
			getCurrentUI()->focusRegained(); // A bit shifty...

			uiNeedsRendering(getCurrentUI());
		}

		if (clip->overdubNature != OverDubType::Normal && playbackHandler.isEitherClockActive()) {
			armClipToStopAction(clip);

			if (clip->getCurrentlyRecordingLinearly() && clip->overdubNature == OverDubType::ContinuousLayering) {
				// Create new clip if we're continuous-layering
				// Make it spawn more too
				currentSong->createPendingNextOverdubBelowClip(clip, clipIndex, OverDubType::ContinuousLayering);
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

/**
 * doLaunch
 *
 * Launches / stops clips at a 'launch event'. This occurs at the end of the current loop in song mode,
 * and additionally to launch fill clips.
 *
 * @param isFillLaunch - A non-fill launch acts on the arm states of all clips:
 * 	                        - Regular clips that are:
 * 								- stopping or starting
 * 								- stopping or starting soloing
 *                              - becoming active or inactive due to some other clip soloing
 *                              - finishing recording
 *                          - Fill clips that are still active and are armed to stop.
 *                       After a regular launch, no regular clips should be armed.
 * 						 Fill clips can remain armed across such a launch, since if
 * 	                     the section repeat count is > 1, or a single clip has had its
 *                       repeat count incread with the select knob, the fill waits until
 *                       the last repeat, whereas doLaunch is called at the end of every
 *                       repeat.
 *
 * 	                     A fill launch starts fill clips at the correct time, such
 *                       that they _finish_ at the next launch event. A fill launch
 *                       should leave non-fill clips unaffected. Fills that launch
 *                       may override other fills if they need the same synth/kit/audio
 *                       output, but must not override a non-fill.
 *
 */
void Session::doLaunch(bool isFillLaunch) {

	if (!isFillLaunch) {
		// For a normal launch, all armed clips either stop or start, and nothing is armed afterwards.
		// For a fill launch this is not true, fills can come in and then immediately arm to stop later,
		// Plus non fills should be unaffected and must remain armed (and visibly so).
		view.flashPlayDisable();
	}
	currentSong->deactivateAnyArrangementOnlyClips(); // In case any still playing after switch from arrangement

	bool anyLinearRecordingBefore = false;
	bool anySoloingAfter = false;
	bool anyClipsStillActiveAfter = false;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	OpenAddressingHashTableWith32bitKey outputsLaunchedFor;

	// First do a loop through all Clips seeing which ones are going to launch, so we can then go through again and
	// deactivate those Outputs' other Clips
	for (int32_t c = currentSong->sessionClips.getNumElements() - 1; c >= 0; c--) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

		if (isFillLaunch
		    && (clip->fillEventAtTickCount <= 0
		        || playbackHandler.lastSwungTickActioned < clip->fillEventAtTickCount)) {
			/* This clip needs no action, since it is not a fill clip,
			   or it is but it's not time to start it, or it's not armed at all. */
			continue;
		}

		// Just gather a tiny bit of other info while we're at it
		anyLinearRecordingBefore = (anyLinearRecordingBefore || clip->getCurrentlyRecordingLinearly());

		// If this one's gonna launch to become active on its output (i.e. not gonna clone its output) when it wasn't
		// before...
		if (clip->armState != ArmState::OFF && !currentSong->isClipActive(clip)
		    && (!clip->isPendingOverdub || !clip->willCloneOutputForOverdub())) {

			Output* output = clip->output;

			if (isFillLaunch && clip->launchStyle == LaunchStyle::FILL && output->getActiveClip()
			    && output->getActiveClip()->launchStyle != LaunchStyle::FILL) {
				/* There's a non fill clip already on this output, don't launch */
				clip->armState = ArmState::OFF;
				continue;
			}

			bool alreadyLaunchedFor = false;
			outputsLaunchedFor.insert((uint32_t)output, &alreadyLaunchedFor);

			// If already seen another Clip to launch with same Output...
			if (alreadyLaunchedFor) {

				// No need to make note of that again, but we do get dibs if we're gonna be soloing
				if (clip->armState == ArmState::ON_TO_SOLO) {
					output->isGettingSoloingClip = true;
				}
			}

			// Or if haven't yet...
			else {
				output->alreadyGotItsNewClip = false;
				output->isGettingSoloingClip = (clip->armState == ArmState::ON_TO_SOLO);
			}
		}

		if (clip->soloingInSessionMode) {
			// If it's not armed, or its arming is just to stop recording, then it's still gonna be soloing afterwards
			if (clip->armState == ArmState::OFF || clip->getCurrentlyRecordingLinearly()) {
				anySoloingAfter = true;
				anyClipsStillActiveAfter = true;
			}
		}
		else {
			if (clip->armState == ArmState::ON_TO_SOLO) {
				anySoloingAfter = true;
				anyClipsStillActiveAfter = true;
			}
			else if (clip->armState == ArmState::ON_NORMAL) {
				if (!clip->activeIfNoSolo || clip->soloingInSessionMode || clip->getCurrentlyRecordingLinearly()) {
					anyClipsStillActiveAfter = true;
				}
			}
			else { // Not armed
				if (clip->soloingInSessionMode || clip->activeIfNoSolo) {
					anyClipsStillActiveAfter = true;
				}
			}
		}
	}

	// Normally it's enough that we set alreadyGotItsNewClip and isGettingSoloingClip on just the Outputs who have a
	// Clip launching. But in the case where soloing is stopping entirely, other Clips are going to be launching, so
	// we'll need to actually set these on all Outputs.
	if (!anySoloingAfter && currentSong->anyClipsSoloing) {
		for (Output* output = currentSong->firstOutput; output; output = output->next) {
			output->alreadyGotItsNewClip = false;
			output->isGettingSoloingClip = false;
		}
	}
	int32_t distanceTilLaunchEvent = 0; // For if clips automatically armed cos they just started recording a loop

	// Ok, now action all currently playing clips. This includes clips armed to start recording or stop - including ones
	// which weren't actually armed to stop but need to stop in order to make way for other ones which were armed to
	// start. But we can't action the starting of any Clips yet, until all stopping is done.
	for (int32_t c = currentSong->sessionClips.getNumElements() - 1; c >= 0; c--) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

		clip->wasActiveBefore = currentSong->isClipActive(clip);

		// If active now (same as before cos we haven't changed it yet)...
		if (clip->wasActiveBefore) {

			bool stoppedLinearRecording = false;
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);
			Output* output = clip->output;

			if (isFillLaunch && clip->launchStyle != LaunchStyle::FILL) {
				/* For a fill launch, ignore all other clips */
				continue;
			}

			// If armed to solo...
			if (clip->armState == ArmState::ON_TO_SOLO) {

				// We were active before, and we'll still be active, so no big change, just this:
				clip->soloingInSessionMode = true;
				clip->armState = ArmState::OFF;

				// If wanting to stop recording linearly at the same time as that...
				if (clip->getCurrentlyRecordingLinearly()) {
					// Won't be a pending overdub - those aren't allowed if we're gonna be soloing
					clip->finishLinearRecording(modelStackWithTimelineCounter, nullptr);
					stoppedLinearRecording = true;
				}
			}

			// start up an overdub
			else if (clip->armState == ArmState::ON_TO_RECORD) {
				if (clip->getCurrentlyRecordingLinearly()) {
					clip->finishLinearRecording(modelStackWithTimelineCounter, nullptr);
					stoppedLinearRecording = true;
				}
				clip->armState = ArmState::OFF;
				clip->setPos(modelStackWithTimelineCounter, 0, false);

				giveClipOpportunityToBeginLinearRecording(clip, c, 0);

				// If that caused it to be armed *again*...
				if (clip->armState == ArmState::ON_NORMAL) {
					distanceTilLaunchEvent = std::max(distanceTilLaunchEvent, clip->loopLength);
				}
			}
			// If armed to stop (these mean stop normally or end soloing respectively)
			else if ((clip->armState == ArmState::ON_NORMAL || clip->armState == ArmState::ON_TO_SOLO)
			         && !isFillLaunch) {

				clip->armState = ArmState::OFF;

				// If output-recording (resampling) is stopping, we don't actually want to deactivate this Clip
				if (playbackHandler.stopOutputRecordingAtLoopEnd) {
					goto stopOnlyIfOutputTaken;
				}

				// Recording linearly
				if (clip->getCurrentlyRecordingLinearly()) {
doFinishLinearRecording:
					Clip* nextPendingOverdub = currentSong->getPendingOverdubWithOutput(clip->output);
					if (nextPendingOverdub) {
						// Copy this again, in case it's changed since it was created
						nextPendingOverdub->copyBasicsFrom(clip);
					}

					clip->finishLinearRecording(modelStackWithTimelineCounter, nextPendingOverdub);
					stoppedLinearRecording = true;

					// After finishing recording linearly, normally we just keep playing.
					goto stopOnlyIfOutputTaken;
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
								goto stopOnlyIfOutputTaken;
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

					if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
						clip->getClipToRecordTo()->endInstance(playbackHandler.getActualArrangementRecordPos(), true);
					}
				}
			}

			// Or if not armed, check we're allowed to still be going
			else {

stopOnlyIfOutputTaken:
				// If some other Clip is gonna start soloing
				if (!currentSong->anyClipsSoloing && anySoloingAfter) {
					if (!stoppedLinearRecording && clip->getCurrentlyRecordingLinearly()) {
						goto doFinishLinearRecording;
					}
					else {
						goto becameInactiveBecauseOfAnotherClipSoloing; // Specifically do not change
						                                                // clip->activeIfNoSolo!
					}
				}

				// If some other Clip is launching for this Output, we gotta stop
				if (outputsLaunchedFor.lookup((uint32_t)output)) {

					if (clip->launchStyle == LaunchStyle::FILL) {
						// Must also disarm it if a fill clip to avoid it
						// re-starting at the eventual launch event.
						clip->armState = ArmState::OFF;
					}

					// If we're linearly recording, we want to stop that as well as ceasing to be active
					if (!stoppedLinearRecording && clip->getCurrentlyRecordingLinearly()) {
						goto doFinishLinearRecording;
					}
					else {
						if (clip->soloingInSessionMode) {
							clip->soloingInSessionMode = false;
							goto becameInactiveBecauseOfAnotherClipSoloing; // Specifically do not change
							                                                // clip->activeIfNoSolo!
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

	// Now's the point where old linear recording has ended, and new is yet to begin. So separate any Actions, for
	// separate undoability
	actionLogger.closeAction(ActionType::RECORD);

	bool sectionWasJustLaunched = (lastSectionArmed < SECTION_OUT_OF_RANGE);
	bool anyLinearRecordingAfter = false;

	// Now action the launching of Clips
	for (int32_t c = currentSong->sessionClips.getNumElements() - 1; c >= 0; c--) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

		if (isFillLaunch
		    && (clip->fillEventAtTickCount <= 0
		        || playbackHandler.lastSwungTickActioned < clip->fillEventAtTickCount)) {
			// In fill launches, ignore everything except fill clips that are launching
			// this very moment.
			continue;
		}

		// Once we laucnh a fill clip, reset its fill event tick count otherwise it will
		// trigger again later.
		clip->fillEventAtTickCount = 0;

		Output* output = clip->output;

		// If we didn't already deal with this Clip, meaning it wasn't active before this launch event...
		if (!clip->wasActiveBefore) {

			bool wasArmedToStartSoloing = (clip->armState == ArmState::ON_TO_SOLO);

			// If it's not armed, normally nothing needs to happen of course - it can just stay inactive
			if (clip->armState == ArmState::OFF) {

				// But if other soloing has stopped and we're suddenly to become active as a result...
				if (!anySoloingAfter && clip->activeIfNoSolo && currentSong->anyClipsSoloing) {
					goto probablyBecomeActive;
				}
			}

			// But if it is armed, to start playing, recording, or soloing...
			else {

				clip->armState = ArmState::OFF;

probablyBecomeActive:
				// If the Output already got its new Clip, then this Clip has missed out and can't become active on it
				if (output->alreadyGotItsNewClip || (output->isGettingSoloingClip && !wasArmedToStartSoloing)) {
					// This clip is not the solo clip

					// But, if we're a pending overdub that's going to clone its Output...
					if (clip->isPendingOverdub && clip->willCloneOutputForOverdub()) {
						goto doNormalLaunch;
					}

					clip->activeIfNoSolo = false;
				}

				// Otherwise, everything's fine and we can launch this Clip
				else {

					output->alreadyGotItsNewClip = true;

doNormalLaunch:
					clip->soloingInSessionMode = wasArmedToStartSoloing;
					if (!wasArmedToStartSoloing) {
						clip->activeIfNoSolo = true;
					}

					ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

					clip->setPos(modelStackWithTimelineCounter, 0, false);

					giveClipOpportunityToBeginLinearRecording(clip, c, 0);
					output = clip->output; // A new Output may have been created as recording began

					// If that caused it to be armed *again*...
					if (clip->armState == ArmState::ON_NORMAL) {
						distanceTilLaunchEvent = std::max(distanceTilLaunchEvent, clip->loopLength);
					}

					// Arm it again if a fill, so it stops at the launchEvent
					if (isFillLaunch && (clip->launchStyle == LaunchStyle::FILL)) {
						clip->armState = ArmState::ON_NORMAL;
					}

					// Must be after giveClipOpportunityToBeginLinearRecording(),
					// cos this call clears any recorded-early notes
					bool activeClipChanged = output->setActiveClip(modelStackWithTimelineCounter);
					if (activeClipChanged) {
						// a new clip has been launched in song view for the current output selected
						// that new clip is now the active clip for that output
						// send updated feedback so that midi controller has the latest values for
						// the current clip selected for midi follow control
						view.sendMidiFollowFeedback();
					}

					if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
						clip->beginInstance(currentSong, playbackHandler.getActualArrangementRecordPos());
					}
				}
			}
		}

		// Arm it again if a ONCE clip, so it stops at the launchEvent
		if (!isFillLaunch && (clip->activeIfNoSolo || clip->soloingInSessionMode)
		    && clip->launchStyle == LaunchStyle::ONCE && clip->armState == ArmState::OFF) {
			clip->armState = ArmState::ON_NORMAL;
			distanceTilLaunchEvent = std::max(distanceTilLaunchEvent, clip->loopLength);
		}

		bool clipActiveAfter = clip->soloingInSessionMode || (clip->activeIfNoSolo && !anySoloingAfter);

		if (clipActiveAfter) {
			anyLinearRecordingAfter = (anyLinearRecordingAfter || clip->getCurrentlyRecordingLinearly());
		}

		// If we found a playing Clip outside of the armed section, or vice versa, then we can't say we legitimately
		// just launched a section
		if (clip->launchStyle != LaunchStyle::FILL && clipActiveAfter != (clip->section == lastSectionArmed)
		    && currentSong->sections[clip->section].numRepetitions != LAUNCH_EXCLUSIVE) {
			sectionWasJustLaunched = false;
		}
	}

	currentSong->anyClipsSoloing = anySoloingAfter;

	if (!isFillLaunch) {
		// If some Clips are playing and they're all in the same section, we want to arm the next section
		if (sectionWasJustLaunched && currentSong->sections[lastSectionArmed].numRepetitions >= 1) {
			armNextSection(lastSectionArmed);
		}

		// Otherwise...
		else {

			bool sectionManuallyStopped = (lastSectionArmed == SECTION_OUT_OF_RANGE);

			lastSectionArmed = REALLY_OUT_OF_RANGE;

			// If no Clips active anymore...
			if (!anyClipsStillActiveAfter) {

				// If we're using the internal clock, we have the power to stop playback entirely
				if (playbackHandler.isInternalClockActive()) {

					// If user is stopping resampling...
					if (playbackHandler.stopOutputRecordingAtLoopEnd) {
						playbackHandler.endPlayback();
					}

					// Or if the action was to manually stop all sections, which could happen if the last section in the
					// song was playing, or if the user ended output recording with playback
					else if (sectionManuallyStopped) {

						// Stop playback entirely
						playbackHandler.endPlayback();

						int32_t sectionToArm;
						// And re-activate the first section
						if (currentSong->sectionToReturnToAfterSongEnd < SECTION_OUT_OF_RANGE) {
							sectionToArm = currentSong->sectionToReturnToAfterSongEnd;
						}
						else {
							Clip* topClip = currentSong->sessionClips.getClipAtIndex(
							    currentSong->sessionClips.getNumElements() - 1);
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
	}

	// If we were doing linear recording before, but we just stopped, then exit RECORD mode, as indicated on LED
	if (anyLinearRecordingBefore && !anyLinearRecordingAfter) {
		if (playbackHandler.recording == RecordingMode::NORMAL) {
			playbackHandler.recording = RecordingMode::OFF;
			playbackHandler.setLedStates();
		}
	}

	AudioEngine::bypassCulling = true;
}

void Session::justAbortedSomeLinearRecording() {
	if (playbackHandler.isEitherClockActive() && currentPlaybackMode == this) {

		for (int32_t c = currentSong->sessionClips.getNumElements() - 1; c >= 0; c--) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

			if (clip->isPendingOverdub || clip->getCurrentlyRecordingLinearly()) {
				return;
			}
		}

		// Exit RECORD mode, as indicated on LED
		if (playbackHandler.recording == RecordingMode::NORMAL) {
			playbackHandler.recording = RecordingMode::OFF;
			playbackHandler.setLedStates();
		}
	}
}

void Session::scheduleLaunchTiming(int64_t atTickCount, int32_t numRepeatsUntil,
                                   int32_t armedLaunchLengthForOneRepeat) {
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

int32_t Session::getNumSixteenthNotesRemainingTilLaunch() {
	float ticksRemaining = static_cast<float>(launchEventAtSwungTickCount - playbackHandler.lastSwungTickActioned
	                                          - playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick());
	float sixteenthNotesRemaining = std::round(ticksRemaining / currentSong->getSixteenthNoteLength());
	// if there is more than 1 repeat remaining, we need to adjust the number of sixteenth notes remaining
	// as number of ticksRemaining is only accurate for 1 repeat
	if (numRepeatsTilLaunch > 1) {
		sixteenthNotesRemaining += (((numRepeatsTilLaunch - 1) * currentArmedLaunchLengthForOneRepeat)
		                            / currentSong->getSixteenthNoteLength());
	}
	return static_cast<int32_t>(sixteenthNotesRemaining);
}

void Session::scheduleFillEvent(Clip* clip, int64_t atTickCount) {
	clip->fillEventAtTickCount = atTickCount;
	int32_t ticksTilFillEvent = atTickCount - playbackHandler.lastSwungTickActioned;
	if (playbackHandler.swungTicksTilNextEvent > ticksTilFillEvent) {
		playbackHandler.swungTicksTilNextEvent = ticksTilFillEvent;
		playbackHandler.scheduleSwungTick();
	}
}

void Session::cancelAllLaunchScheduling() {
	launchEventAtSwungTickCount = 0;
}

void Session::launchSchedulingMightNeedCancelling() {
	if (!preLoadedSong && !areAnyClipsArmed()) {
		cancelAllLaunchScheduling();
		if (display->haveOLED()) {
			RootUI* rootUI = getRootUI();
			if (loadSongUI.isLoadingSong()) {
				loadSongUI.displayLoopsRemainingPopup(); // Wait, could this happen?
			}
			else if ((rootUI == &sessionView || rootUI == &performanceView)
			         && !isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)) {
				renderUIsForOled();
			}
		}
		else {
			sessionView.redrawNumericDisplay();
		}
	}
}

// Taking sync-scaling and the Clip's length into account, puts us at the place in the Clip as if playback had occurred
// under these conditions since the input clock started. Presumably we'd call this if the conditions have changed (e.g.
// sync-scaling changed) and we want to restore order
void Session::reSyncClipToSongTicks(Clip* clip) {

	if (clip->armState != ArmState::OFF) {
		clip->armState = ArmState::OFF;
		launchSchedulingMightNeedCancelling();
	}

	// If Clip inactive, nothing to do
	if (!currentSong->isClipActive(clip)) {
		return;
	}

	// I've sort of forgotten why this bit here is necessary (well, I know it deals with the skipping of ticks). Could
	// it just be put into the setPos() function? I basically copied this from Editor::launchClip()
	int32_t modifiedStartPos = (int32_t)playbackHandler.lastSwungTickActioned;
	while (modifiedStartPos < 0) {
		modifiedStartPos += clip->loopLength; // Fairly unlikely I think
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);

	clip->setPos(modelStack, modifiedStartPos);

	clip->resumePlayback(modelStack);
}

// Will appropriately change a Clip's play-pos to sync it to input MIDI clock or another appropriate Clip's play-pos.
// Currently called only when a Clip is created or resized. In some cases, it'll determine that it doesn't want to
// resync the Clip - e.g. if there's nothing "nice" to sync it to. But if mustSetPosToSomething is supplied as true,
// then we'll make sure we still set the pos to something / sync it to something (because it presumably didn't have a
// valid pos yet). Check playbackHandler.isEitherClockActive() before calling this.
void Session::reSyncClip(ModelStackWithTimelineCounter* modelStack, bool mustSetPosToSomething, bool mayResumeClip) {

	Clip* clip = (Clip*)modelStack->getTimelineCounter();

	bool armingCancelled = clip->cancelAnyArming();
	if (armingCancelled) {
		launchSchedulingMightNeedCancelling();
	}

	if (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(clip)) {

		// If following external clock...
		if (playbackHandler.isExternalClockActive()) {

			// If this is in fact the sync-scaling Clip, we want to resync all Clips. The song will have already updated
			// its inputTickMagnitude
			if (clip == modelStack->song->getSyncScalingClip()) {
				playbackHandler.resyncInternalTicksToInputTicks(modelStack->song);
			}

			// Otherwise, set its position according to the incoming clock count. (Wait, why on earth do I have it not
			// syncing to other Clips in such cases? Some weird historical Deluge relic?)
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

		// Or if playing from internal clock, then try to sync to another Clip with a similar-looking length (i.e.
		// hopefully the same time signature)
		else {
			Clip* syncToClip =
			    modelStack->song->getLongestActiveClipWithMultipleOrFactorLength(clip->loopLength, false, clip);
			if (syncToClip) {

				int32_t oldPos = clip->lastProcessedPos;
				clip->setPos(modelStack, syncToClip->getCurrentPosAsIfPlayingInForwardDirection());
				int32_t newPos = clip->lastProcessedPos;

				// Only call "resume" if pos actually changed. This way, we can save some dropping out of AudioClips
				if (oldPos != newPos || mustSetPosToSomething) {
					if (mayResumeClip) {
						clip->resumePlayback(modelStack);
					}
				}
				else {
					goto doAudioClipStuff;
				}
			}
			else {
				if (mustSetPosToSomething) {
					goto doReSyncToSongTicks;
				}

				// For AudioClips, even if we're not gonna call resumePlayback(), we still need to do some other stuff
				// if length has been changed (which it probably has if we're here)
doAudioClipStuff:
				if (clip->type == ClipType::AUDIO) {
					((AudioClip*)clip)->setupPlaybackBounds();
					((AudioClip*)clip)->sampleZoneChanged(modelStack);
				}
			}
		}
	}
}

void Session::userWantsToUnsoloClip(Clip* clip, bool forceLateStart, int32_t buttonPressLatency) {
	// If Deluge not playing session, easy
	if (!hasPlaybackActive()) {
doUnsolo:
		unsoloClip(clip);
	}

	// Or if Deluge *is* playing session...
	else {

		if (forceLateStart) {
			if (!playbackHandler.isEitherClockActive()) {
				goto doTempolessRecording;
			}
			else {
				goto doUnsolo;
			}
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

				// armClips0(0, clip, false, forceLateStart); // Force "late start" if user holding shift button
				clip->armState = ArmState::ON_NORMAL;
				int64_t wantToStopAtTime =
				    playbackHandler.getActualSwungTickCount()
				    - clip->getClipToRecordTo()->getActualCurrentPosAsIfPlayingInForwardDirection() + clip->loopLength;
				scheduleLaunchTiming(wantToStopAtTime, 1, clip->loopLength);
			}
		}
	}
}

// clipIndex is optional
void Session::cancelArmingForClip(Clip* clip, int32_t* clipIndex) {
	clip->armState = ArmState::OFF;

	if (clip->getCurrentlyRecordingLinearly()) {
		bool anyDeleted = currentSong->deletePendingOverdubs(clip->output, clipIndex);
		if (anyDeleted) {
			// use root UI in case this is called from performance view
			sessionView.requestRendering(getRootUI());
		}
	}

	launchSchedulingMightNeedCancelling();
}

// Beware - calling this might insert or delete a Clip! E.g. if we disarm a Clip that had a pending overdub, the overdub
// will get deleted. clipIndex is optional.
void Session::toggleClipStatus(Clip* clip, int32_t* clipIndex, bool doInstant, int32_t buttonPressLatency) {

	// Not allowed if playing arrangement
	if (playbackHandler.playbackState && currentPlaybackMode == &arrangement) {
		return;
	}

	if (currentSong->sections[clip->section].numRepetitions != LAUNCH_EXCLUSIVE) {
		lastSectionArmed = REALLY_OUT_OF_RANGE;
	}

	// If Clip armed, cancel arming - but not if it's an "instant" toggle
	if (clip->launchStyle == LaunchStyle::FILL && clip->armState != ArmState::OFF && !clip->isActiveOnOutput()) {
		// Fill clips can be disarmed (to start) if they haven't started yet
		// Allowing the user to disarm them while armed to stop risks them
		// getting stuck on after the launchEvent.
		cancelArmingForClip(clip, clipIndex);
	}
	else if (clip->armState != ArmState::OFF && !doInstant) {
		cancelArmingForClip(clip, clipIndex);
	}

	// If Clip soloing
	else if (clip->soloingInSessionMode) {
		userWantsToUnsoloClip(clip, doInstant, buttonPressLatency);
	}

	// Or, if some other Clip is soloed, just toggle the playing status - it won't make a difference
	else if (currentSong->getAnyClipsSoloing()) {
		clip->activeIfNoSolo = !clip->activeIfNoSolo;

		// If became "active" (in background behind soloing), need to "deactivate" any other Clips - still talking about
		// in the "background" here.
		if (clip->activeIfNoSolo) {
			// For each Clip in session
			for (int32_t c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
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
					RootUI* rootUI = getRootUI();
					if (rootUI == &sessionView || rootUI == &performanceView) {
						uiNeedsRendering(rootUI, 0, 0xFFFFFFFF);
					}
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
					if (doInstant || clip->launchStyle == LaunchStyle::FILL) {
						if (clip->armState != ArmState::OFF) { // In case also already armed
							clip->armState = ArmState::OFF;
							launchSchedulingMightNeedCancelling();
						}

						// Linear recording - stopping instantly in this case means reducing the Clip's length and
						// arming to stop recording real soon, at next tick
						if (clip->getCurrentlyRecordingLinearly()) {
							cancelAllArming();
							cancelAllLaunchScheduling();
							Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);
							currentSong->setClipLength(clip, clip->getLivePos() + 1, action,
							                           false); // Tell it not to resync
							armClipToStopAction(clip);

							sessionView.clipNeedsReRendering(clip);
							if (getCurrentClip()) {
								if (getCurrentClip()->onAutomationClipView) {
									uiNeedsRendering(&automationView, 0xFFFFFFFF, 0);
								}
								else {
									uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
								}
							}
						}

						// Or normal
						else {
							clip->expectNoFurtherTicks(currentSong);

							if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
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
	clip->armState = ArmState::ON_NORMAL;

	int32_t actualCurrentPos = (uint32_t)clip->getClipToRecordTo()->getActualCurrentPosAsIfPlayingInForwardDirection()
	                           % (uint32_t)clip->loopLength;
	int64_t wantToStopAtTime = playbackHandler.getActualSwungTickCount() - actualCurrentPos + clip->loopLength;

	scheduleLaunchTiming(wantToStopAtTime, 1, clip->loopLength);
}

void Session::soloClipAction(Clip* clip, int32_t buttonPressLatency) {
	lastSectionArmed = REALLY_OUT_OF_RANGE;

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
				// use root UI in case this is called from performance view
				sessionView.requestRendering(getRootUI(), 0, 0xFFFFFFFF);
				goto renderAndGetOut;
			}
		}

		else {
			userWantsToArmClipsToStartOrSolo(0, clip, false, Buttons::isShiftButtonPressed(), true, 1, true,
			                                 ArmState::ON_TO_SOLO); // Force "late start" if user holding shift button
		}
	}

	armingChanged();

renderAndGetOut:
	if (anyClipsDeleted) {
		sessionView.requestRendering(getRootUI());
	}
}

void Session::armSection(uint8_t section, int32_t buttonPressLatency) {

	// Get rid of soloing. And if we're not a "share" section, get rid of arming too
	currentSong->turnSoloingIntoJustPlaying(currentSong->sections[section].numRepetitions >= 0);

	// If every Clip in this section is already playing, and no other Clips are (unless we're a "share" section), then
	// there's no need to launch the section because it's already playing. So, make sure this isn't the case before we
	// go and do anything more
	for (int32_t c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

		// If a Clip in the section is not playing...
		if (clip->section == section && !clip->activeIfNoSolo) {
			goto yupThatsFine; // Remember, we cancelled any soloing, above
		}

		// If a Clip in another section is playing and we're not a "share" section...
		if (currentSong->sections[section].numRepetitions > -1 && clip->section != section
		    && ((clip->armState != ArmState::OFF) != clip->activeIfNoSolo)) {
			goto yupThatsFine;
		}
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
		    section, nullptr, stopAllOtherClips, false,
		    false); // Don't allow "late start". It's too fiddly to implement, and rarely even useful for sections

		if (currentSong->sections[section].numRepetitions != LAUNCH_EXCLUSIVE) {
			lastSectionArmed = section;
		}
	}

	armingChanged();
}

// Probably have to call armingChanged() after this.
// Can sorta be applicable either then !playback state, or when tempoless recording.
void Session::armSectionWhenNeitherClockActive(ModelStack* modelStack, int32_t section, bool stopAllOtherClips) {
	for (int32_t c = 0; c < modelStack->song->sessionClips.getNumElements(); c++) {
		Clip* clip = modelStack->song->sessionClips.getClipAtIndex(c);

		if (clip->section == section && !clip->activeIfNoSolo) {
			clip->activeIfNoSolo = true;

			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			modelStack->song->assertActiveness(modelStackWithTimelineCounter);
		}

		if (stopAllOtherClips && clip->section != section && clip->activeIfNoSolo
		    && currentSong->sections[clip->section].numRepetitions != LAUNCH_EXCLUSIVE) {
			// thisClip->expectNoFurtherTicks(currentSong); // No, don't need this, cos it's not playing!
			clip->activeIfNoSolo = false;
		}
	}
}

// Updates LEDs after arming changed
void Session::armingChanged() {
	RootUI* rootUI = getRootUI();
	if (rootUI == &sessionView || rootUI == &performanceView) {
		sessionView.requestRendering(rootUI, 0, 0xFFFFFFFF);

		if (getCurrentUI()->canSeeViewUnderneath()) {
			if (display->haveOLED()) {
				if (!isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)
				    && !isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
					renderUIsForOled();
				}
			}
			else {
				sessionView.redrawNumericDisplay();
			}
probablyDoFlashPlayEnable:
			if (hasPlaybackActive()) {
				view.flashPlayEnable();
			}
		}
	}
	else {
		// TODO: only if sidebar visible!
		uiNeedsRendering(getCurrentUI(), 0x00000000, 0xFFFFFFFF);
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
	LaunchStatus launchStatus =
	    investigateSyncedLaunch(waitForClip, &currentPosWithinQuantization, &quantization, overdub->loopLength, true);

	// If nothing to sync to, which means no other Clips playing...
	if (launchStatus == LaunchStatus::NOTHING_TO_SYNC_TO) {
		playbackHandler.endPlayback();
		playbackHandler.setupPlaybackUsingInternalClock(); // We're restarting playback, but it was already happening,
		                                                   // so no need for PGMs
	}

	// This case, too, can only actually happen if no Clips are playing
	else if (launchStatus == LaunchStatus::LAUNCH_ALONG_WITH_EXISTING_LAUNCHING) {}

	else { // LaunchStatus::LAUNCH_USING_QUANTIZATION
		currentPosWithinQuantization = currentPosWithinQuantization % quantization;
		uint32_t ticksTilStart = quantization - currentPosWithinQuantization;
		int64_t launchTime = playbackHandler.getActualSwungTickCount() + ticksTilStart;

		scheduleLaunchTiming(launchTime, 1, quantization);
	}

	armingChanged();
}

void Session::armClipsAlongWithExistingLaunching(ArmState armState, uint8_t section, Clip* clip) {
	// If just one Clip...
	if (clip) {
		armClipLowLevel(clip, armState);
	}
	else // Or, if a whole section...
	{
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

void Session::armClipsWithNothingToSyncTo(uint8_t section, Clip* clip) {
	playbackHandler.endPlayback();
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	// Restart playback.
	// If just one Clip...
	if (clip) {
		clip->activeIfNoSolo = true;
		currentSong->assertActiveness(modelStack->addTimelineCounter(clip));
	}
	else // Or, if a whole section...
	{
		for (int c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
			Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);
			if (thisClip->section == section) {
				thisClip->activeIfNoSolo = true;
				currentSong->assertActiveness(modelStack->addTimelineCounter(thisClip)); // Very inefficient
			}
		}
	}
	playbackHandler.setupPlaybackUsingInternalClock(); // We're restarting playback, but it was already happening, so no
	                                                   // need for PGMs
}

// This can only be called if the Deluge is currently playing
void Session::userWantsToArmClipsToStartOrSolo(uint8_t section, Clip* clip, bool stopAllOtherClips, bool forceLateStart,
                                               bool allowLateStart, int32_t newNumRepeatsTilLaunch,
                                               bool allowSubdividedQuantization, ArmState armState) {

	// Find longest starting Clip length, and what Clip we're waiting on
	uint32_t longestStartingClipLength;
	Clip* waitForClip;

	// ... if launching just a Clip
	if (clip) {

		// Now (Nov 2020), we're going to call getLongestActiveClipWithMultipleOrFactorLength() for all launching of a
		// Clip (not a section). It seems that for the past several years(?) this was broken and would always just wait
		// for the longest Clip.
		waitForClip = currentSong->getLongestActiveClipWithMultipleOrFactorLength(
		    clip->loopLength); // Allow it to return our same Clip if it wants - and if it's active, which could be what
		                       // we want in the case of arming-to-solo if Clip already active

		if (clip->launchStyle == LaunchStyle::FILL) {
			longestStartingClipLength = waitForClip->loopLength;
		}
		else {
			longestStartingClipLength = clip->loopLength;
		}
	}

	// ... or if launching a whole section
	else {

		// We don't want to call getLongestActiveClipWithMultipleOrFactorLength(), because when launching a new section,
		// the length of any of the new Clips being launched is irrelevant, cos they won't be playing at the same time
		// as any previously playing ones (normally). Also, this is how it seems to have worked for several years(?)
		// until the bugfix above, and I wouldn't want the behaviour changing on any users.
		waitForClip = currentSong->getLongestClip(false, true);
		longestStartingClipLength = 0;
		for (int32_t c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
			Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);

			if (thisClip->section == section && thisClip->loopLength > longestStartingClipLength) {
				longestStartingClipLength = thisClip->loopLength;
			}
		}

		// If section was empty, use longest clip waitForClip length instead
		if (longestStartingClipLength == 0) {
			longestStartingClipLength = waitForClip->loopLength;
		}
	}

	uint32_t quantization;
	uint32_t currentPosWithinQuantization;
	LaunchStatus launchStatus = investigateSyncedLaunch(waitForClip, &currentPosWithinQuantization, &quantization,
	                                                    longestStartingClipLength, allowSubdividedQuantization);

	// If nothing to sync to, which means no other Clips playing...
	if (launchStatus == LaunchStatus::NOTHING_TO_SYNC_TO) {
		armClipsWithNothingToSyncTo(section, clip);
	}

	// This case, too, can only actually happen if no Clips are playing
	else if (launchStatus == LaunchStatus::LAUNCH_ALONG_WITH_EXISTING_LAUNCHING) {
		armClipsAlongWithExistingLaunching(armState, section, clip);
	}

	else { // LaunchStatus::LAUNCH_USING_QUANTIZATION
		armClipsToStartOrSoloWithQuantization(currentPosWithinQuantization, quantization, section, stopAllOtherClips,
		                                      clip, forceLateStart, allowLateStart, newNumRepeatsTilLaunch, armState);

		if (clip) {
			scheduleFillClip(clip);
		}
		else {
			scheduleFillClips(section);
		}
	}
}

LaunchStatus Session::investigateSyncedLaunch(Clip* waitForClip, uint32_t* currentPosWithinQuantization,
                                              uint32_t* quantization, uint32_t longestStartingClipLength,
                                              bool allowSubdividedQuantization) {

	// If no Clips are playing...
	if (!waitForClip) {

		// See if any other Clips are armed. We can start at the same time as them
		if (launchEventAtSwungTickCount) {
			return LaunchStatus::LAUNCH_ALONG_WITH_EXISTING_LAUNCHING;

			// Otherwise...
		}
		else {

			// If a clock is coming in or out, or metronome is on, use that to work out the loop point
			if (playbackHandler.isExternalClockActive() || playbackHandler.midiOutClockEnabled
			    || playbackHandler.metronomeOn
			    || cvEngine.gateChannels[WHICH_GATE_OUTPUT_IS_CLOCK].mode == GateType::SPECIAL
			    || playbackHandler.recording == RecordingMode::ARRANGEMENT) {

				uint32_t oneBar = currentSong->getBarLength();

				// If using internal clock (meaning metronome or clock output is on), just quantize to one bar. This is
				// potentially imperfect.
				if (playbackHandler.isInternalClockActive()) {
					*quantization = oneBar;
				}

				// Otherwise, quantize to: the sync scale, magnified up to be at least 3 beats long
				else {

					// Work out the length of 3 beats, given the length of 1 bar. Or if 1 bar is already too short, just
					// use that, to avoid bugs
					uint32_t threeBeats;
					if (oneBar >= 2) {
						threeBeats = (oneBar * 3) >> 2;
					}
					else {
						threeBeats = oneBar;
					}

					*quantization = currentSong->getInputTickScale();
					while (*quantization < threeBeats) {
						*quantization <<= 1;
					}
				}

				*currentPosWithinQuantization = playbackHandler.getActualSwungTickCount();
				return LaunchStatus::LAUNCH_USING_QUANTIZATION;
			}

			// Or if using internal clock with no metronome or clock incoming or outgoing, then easy - we can really
			// just restart playback
			else {
				return LaunchStatus::NOTHING_TO_SYNC_TO;
			}
		}
	}

	// Or, the more normal case where some Clips were already playing
	else {

		// Quantize the launch to the length of the already-playing long Clip, or if the new Clip / section fits into it
		// a whole number of times, use that length
		if (allowSubdividedQuantization && longestStartingClipLength < waitForClip->loopLength
		    && ((uint32_t)waitForClip->loopLength % longestStartingClipLength) == 0) {
			*quantization = longestStartingClipLength;
		}
		else {
			*quantization = waitForClip->loopLength;
		}

		*currentPosWithinQuantization =
		    waitForClip->getClipToRecordTo()->getActualCurrentPosAsIfPlayingInForwardDirection();
		return LaunchStatus::LAUNCH_USING_QUANTIZATION;
	}
}

// Returns whether we are now armed. If not, it means it's just done the swap already in this function
bool Session::armForSongSwap() {
	D_PRINTLN("Session::armForSongSwap()");

	Clip* waitForClip = currentSong->getLongestClip(false, true);

	uint32_t quantization;
	uint32_t currentPosWithinQuantization;
	LaunchStatus launchStatus =
	    investigateSyncedLaunch(waitForClip, &currentPosWithinQuantization, &quantization, 0xFFFFFFFF, false);

	// If nothing to sync to, just do the swap right now
	if (launchStatus == LaunchStatus::NOTHING_TO_SYNC_TO) {
		playbackHandler.doSongSwap();
		playbackHandler.endPlayback();
		playbackHandler.setupPlaybackUsingInternalClock(); // No need to send PGMs - they're sent in doSongSwap().
		return false;
	}

	else if (launchStatus == LaunchStatus::LAUNCH_USING_QUANTIZATION) {
		int32_t pos = currentPosWithinQuantization % quantization;
		int32_t ticksTilSwap = quantization - pos;
		scheduleLaunchTiming(playbackHandler.getActualSwungTickCount() + ticksTilSwap, 1, quantization);
		D_PRINTLN("ticksTilSwap:  %d", ticksTilSwap);
	}
	else if (launchStatus == LaunchStatus::LAUNCH_ALONG_WITH_EXISTING_LAUNCHING) {
		// Nothing to do!
	}

	return true;
}

// Returns whether we are now armed. If not, it means it's just done the swap already in this function
bool Session::armForSwitchToArrangement() {

	Clip* waitForClip = currentSong->getLongestClip(false, true);

	uint32_t quantization;
	uint32_t currentPosWithinQuantization;
	LaunchStatus launchStatus =
	    investigateSyncedLaunch(waitForClip, &currentPosWithinQuantization, &quantization, 2147483647, false);

	if (launchStatus == LaunchStatus::NOTHING_TO_SYNC_TO) {
		playbackHandler.switchToArrangement();
		return false;
	}

	else if (launchStatus == LaunchStatus::LAUNCH_USING_QUANTIZATION) {
		int32_t pos = currentPosWithinQuantization % quantization;
		int32_t ticksTilSwap = quantization - pos;
		scheduleLaunchTiming(playbackHandler.getActualSwungTickCount() + ticksTilSwap, 1, quantization);
		switchToArrangementAtLaunchEvent = true;
	}
	else if (launchStatus == LaunchStatus::LAUNCH_ALONG_WITH_EXISTING_LAUNCHING) {
		switchToArrangementAtLaunchEvent = true;
	}

	return true;
}

void Session::armClipsToStartOrSoloWithQuantization(uint32_t pos, uint32_t quantization, uint8_t section,
                                                    bool stopAllOtherClips, Clip* clip, bool forceLateStart,
                                                    bool allowLateStart, int32_t newNumRepeatsTilLaunch,
                                                    ArmState armState) {

	// We want to allow the launch point to be a point "within" the longest Clip, at multiple lengths of our shortest
	// launching Clip
	pos = pos % quantization;

	bool doLateStart = forceLateStart;

	// If we were doing this just for one Clip (so a late-start might be allowed too)...
	if (clip) {
		if (clip->launchStyle == LaunchStyle::DEFAULT || !doLateStart) {
			if (!doLateStart && allowLateStart) { // Reminder - late start is never allowed for sections - just cos it's
				                                  // not that useful, and tricky to implement

				// See if that given point was only just reached a few milliseconds ago - in which case we'll do a "late
				// start"
				uint32_t timeAgo = pos * playbackHandler.getTimePerInternalTick(); // Accurate enough
				doLateStart = (timeAgo < kAmountNoteOnLatenessAllowed);
			}

			armClipToStartOrSoloUsingQuantization(clip, doLateStart, pos, armState);
		}
	}

	// Or, if we were doing it for a whole section - which means that we know armState == ArmState::ON_NORMAL, and no
	// late-start
	else {
		OpenAddressingHashTableWith32bitKey outputsWeHavePickedAClipFor;

		// Ok, we're going to do a big complex thing where we traverse just once (or occasionally twice) through all
		// sessionClips. Reverse order so behaviour of this new code is the same as the old code
		for (int32_t c = currentSong->sessionClips.getNumElements() - 1; c >= 0; c--) {
			Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);

			if (thisClip->launchStyle == LaunchStyle::FILL) {
				continue;
			}

			// If thisClip is in the section we're wanting to arm...
			if (thisClip->section == section) {
				// Because we're arming a section, we know there's no soloing Clips, so that's easy.
				Output* output = thisClip->output;

				bool alreadyPickedAClip = false;
				outputsWeHavePickedAClipFor.insert((uint32_t)output, &alreadyPickedAClip);

				// If we've already picked a Clip for this same Output...
				if (alreadyPickedAClip) {

					if (!output->nextClipFoundShouldGetArmed) {
						if (thisClip->activeIfNoSolo) {
							output->nextClipFoundShouldGetArmed = true;
						}
						goto weWantThisClipInactive;
					}
					else {

						// We're gonna make this Clip active, but we may have already tried to make a previous
						// one on this Output active, so go back through all previous ones and deactivate / disarm them
						for (int32_t d = currentSong->sessionClips.getNumElements() - 1; d > c; d--) {
							Clip* thatClip = currentSong->sessionClips.getClipAtIndex(d);
							if (thatClip->output == output) {
								thatClip->armState = thatClip->activeIfNoSolo ? ArmState::ON_NORMAL : ArmState::OFF;
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
						if (thisClip->armState != ArmState::OFF && thisClip->launchStyle != LaunchStyle::ONCE) {
							thisClip->armState = ArmState::OFF;
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
					// Clip is launched exclusively
					if (currentSong->sections[thisClip->section].numRepetitions == LAUNCH_EXCLUSIVE) {
						if (thisClip->activeIfNoSolo) {
							thisClip->armState = ArmState::OFF;
						}
					}

					// If it's active, arm it to stop
					else if (thisClip->activeIfNoSolo) {
						thisClip->armState = ArmState::ON_NORMAL;
					}

					// Or if it's already inactive...
					else {
						// If it's armed to start, cancel that
						if (thisClip->armState != ArmState::OFF) {
							thisClip->armState = ArmState::OFF;
						}
					}
				}

				// Or, if we don't want to stop it because of its section, we'll need to make sure that it just doesn't
				// share an Output with one of the clips which is gonna get launched, in our new section. Unfortunately,
				// we haven't seen all of those yet, so we'll have to come through and do this in a separate, second
				// traversal - see next comment below
			}
		}

		// Ok, and as mentioned in the big comment directly above, if we're not doing a stopAllOtherClips, only now are
		// we in a position to know which of those other Clips we still will need to stop because of their Output
		if (!stopAllOtherClips) {

			for (int32_t c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
				Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);

				// Ok, so if it's from another section (only those, because we've already dealt with the ones in our
				// section)...
				if (thisClip->section != section) {

					// And if it's currently active...
					if (thisClip->activeIfNoSolo) {

						// If we've already picked a Clip for this same Output, we definitely don't want this one
						// remaining active, so arm it to stop
						if (outputsWeHavePickedAClipFor.lookup((uint32_t)thisClip->output)) {
							thisClip->armState = ArmState::ON_NORMAL;
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
void Session::armClipToStartOrSoloUsingQuantization(Clip* thisClip, bool doLateStart, uint32_t pos, ArmState armState,
                                                    bool mustUnarmOtherClipsWithSameOutput) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, thisClip);

	// Arm to start soloing
	if (armState == ArmState::ON_TO_SOLO) {
		// If need to enact change instantly...
		if (doLateStart) {

			// If that was already armed, un-arm it
			if (thisClip->armState != ArmState::OFF) {
				thisClip->armState = ArmState::OFF;
				launchSchedulingMightNeedCancelling();
			}

			// currentSong->deactivateAnyArrangementOnlyClips(); // In case any left playing after switch from
			// arrangement

			bool wasAlreadyActive = currentSong->isClipActive(thisClip);

			soloClipRightNow(modelStack);

			if (!wasAlreadyActive) {
				goto setPosAndStuff;
			}
		}

		// Otherwise, arm it
		else {
			armClipLowLevel(thisClip, ArmState::ON_TO_SOLO);
		}
	}

	// Arm to start regular play
	else {

		// If late start...
		if (doLateStart) {

			if (thisClip->armState != ArmState::OFF
			    && thisClip->launchStyle == LaunchStyle::DEFAULT) { // In case also already armed
				thisClip->armState = ArmState::OFF;
				launchSchedulingMightNeedCancelling();
			}

			thisClip->activeIfNoSolo = true;

			// Must call this before setPos, because that does stuff with ParamManagers
			currentSong->assertActiveness(modelStack, playbackHandler.getActualArrangementRecordPos() - pos);

setPosAndStuff:
			// pos is a "live" pos, so we have to subtract swungTicksSkipped before setting the Clip's lastProcessedPos,
			// because it's soon going to be jumped forward by that many ticks
			int32_t modifiedStartPos = (int32_t)pos - playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick();
			thisClip->setPos(modelStack, modifiedStartPos);

			thisClip->resumePlayback(modelStack);

			// If recording session to arranger, do that
			if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
				thisClip->beginInstance(currentSong, playbackHandler.getActualArrangementRecordPos() - pos);
			}
		}

		// Or if normal start...
		else {
			armClipLowLevel(thisClip, ArmState::ON_NORMAL, mustUnarmOtherClipsWithSameOutput);
		}
	}
}

/**
 * scheduleFillClip - schedules a fill clip. It must reach its endpoint
 *                    at the next launch event, ie right aligned to the
 *                    usual song mode loop. If there is less time until
 *                    then than the clip is long, start right now mid way
 *                    through. Otherwise schedule for the correct time.
 *
 * @param clip - the clip to launch.
 *
 */
void Session::scheduleFillClip(Clip* clip) {

	if (clip->launchStyle == LaunchStyle::FILL) {
		if (launchEventAtSwungTickCount > 0) {

			int64_t fillStartTime = launchEventAtSwungTickCount - clip->getMaxLength()
			                        + (numRepeatsTilLaunch - 1) * currentArmedLaunchLengthForOneRepeat;
			// Work out if a 'launchEvent' is already scheduled,
			// and if closer than the fill clip length, do immediate launch */
			// if (launchEventAtSwungTickCount < playbackHandler.getActualSwungTickCount() + clip->getMaxLength()) {
			if (fillStartTime < playbackHandler.getActualSwungTickCount()) {
				if (clip->output->getActiveClip()) {
					if (clip->output->getActiveClip()->launchStyle == LaunchStyle::FILL) {
						/* A fill clip is already here, steal the output */
					}
					else {
						/* Something else is on this output and is not a fill, leave it alone.*/
						return;
					}
				}

				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);
				uint32_t pos;
				pos = clip->getMaxLength() - (launchEventAtSwungTickCount - playbackHandler.getActualSwungTickCount());

				if (clip->armState != ArmState::OFF) { // In case also already armed
					clip->armState = ArmState::OFF;
				}

				clip->activeIfNoSolo = true;

				// Must call this before setPos, because that does stuff with ParamManagers
				currentSong->assertActiveness(modelStack, playbackHandler.getActualArrangementRecordPos() - pos);

				// pos is a "live" pos, so we have to subtract swungTicksSkipped before setting the Clip's
				// lastProcessedPos, because it's soon going to be jumped forward by that many ticks
				int32_t modifiedStartPos =
				    (int32_t)pos - playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick();
				clip->setPos(modelStack, modifiedStartPos);

				clip->resumePlayback(modelStack);

				// If recording session to arranger, do that
				if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
					clip->beginInstance(currentSong, playbackHandler.getActualArrangementRecordPos() - pos);
				}

				/* Arm to stop once started */
				clip->armState = ArmState::ON_NORMAL;
			}
			else {
				scheduleFillEvent(clip, fillStartTime);
				armClipLowLevel(clip, ArmState::ON_NORMAL, false);
			}
		}
	}
}

/**
 * scheduleFillClips - schedules all fill clips in a section. If
 *                     a non-fill clip is already playing on the same output
 *                     that we want to use, we prevent the fill from starting.
 *
 * @param section - the section number to launch fill clips for.
 *
 */
void Session::scheduleFillClips(uint8_t section) {
	for (int32_t c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
		Clip* thisClip = currentSong->sessionClips.getClipAtIndex(c);

		// If thisClip is in the section we're wanting to arm...
		if (thisClip->section == section && thisClip->launchStyle == LaunchStyle::FILL) {

			Output* output = thisClip->output;
			if (output->getActiveClip() && output->getActiveClip()->launchStyle != LaunchStyle::FILL) {
				/* Some non-fill already has this output. We can't steal it.*/
				continue;
			}
			scheduleFillClip(thisClip);
		}
	}
}

void Session::cancelAllArming() {
	for (int32_t l = 0; l < currentSong->sessionClips.getNumElements(); l++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(l);
		clip->cancelAnyArming();
	}
}

void Session::armClipLowLevel(Clip* clipToArm, ArmState armState, bool mustUnarmOtherClipsWithSameOutput) {

	clipToArm->armState = armState;

	// Unarm any armed Clips with same Output, if we're doing that
	if (mustUnarmOtherClipsWithSameOutput) {
		// All session Clips
		for (int32_t c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

			if (clip != clipToArm && !clip->soloingInSessionMode && !clip->activeIfNoSolo
			    && clip->armState != ArmState::OFF && clip->output == clipToArm->output) {
				clip->armState = ArmState::OFF;
			}
		}
	}
}

int32_t Session::userWantsToArmNextSection(int32_t numRepetitions) {

	int32_t currentSection = getCurrentSection();
	if (currentSection < SECTION_OUT_OF_RANGE) {
		if (numRepetitions <= -1) {
			numRepetitions = currentSong->sections[currentSection].numRepetitions;
		}

		if (numRepetitions >= 1) {
			armNextSection(currentSection, numRepetitions);
			armingChanged();
		}
	}
	return currentSection;
}

// Only returns result if all Clips in section are playing, and no others
// Exactly what the return values of 255 and 254 mean has been lost, but they're treated as interchangeable by the
// function that calls this anyway
int32_t Session::getCurrentSection() {

	if (currentSong->getAnyClipsSoloing()) {
		return REALLY_OUT_OF_RANGE;
	}

	int32_t section = REALLY_OUT_OF_RANGE;

	bool anyUnlaunchedLoopablesInSection[kMaxNumSections];
	memset(anyUnlaunchedLoopablesInSection, 0, sizeof(anyUnlaunchedLoopablesInSection));

	for (int32_t l = 0; l < currentSong->sessionClips.getNumElements(); l++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(l);

		// Launch exclusively section
		if (currentSong->sections[clip->section].numRepetitions == LAUNCH_EXCLUSIVE) {
			continue;
		}

		if (clip->activeIfNoSolo) {
			if (section == REALLY_OUT_OF_RANGE) {
				section = clip->section;
			}
			else if (section != clip->section) {
				return SECTION_OUT_OF_RANGE;
			}
		}
		else {
			if (ALPHA_OR_BETA_VERSION && clip->section > kMaxNumSections) {
				FREEZE_WITH_ERROR("E243");
			}
			anyUnlaunchedLoopablesInSection[clip->section] = true;
		}
	}

	if (anyUnlaunchedLoopablesInSection[section]) {
		return REALLY_OUT_OF_RANGE;
	}
	return section;
}

bool Session::areAnyClipsArmed() {
	for (int32_t l = 0; l < currentSong->sessionClips.getNumElements(); l++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(l);

		if (clip->armState != ArmState::OFF) {
			return true;
		}
	}

	return false;
}

// This is a little bit un-ideal, but after an undo or redo, this will be called, and it will tell every active Clip
// to potentially expect a note or automation event - and to re-get all current automation values.
// I wish we could easily just do this to the Clips that need it, but we don't store an easy list of just the Clips
// affected by each Action. This is only to be called if playbackHandler.isEitherClockActive().
void Session::reversionDone() {
	if (!currentSong) {
		return;
	}
	for (Clip* clip : AllClips::everywhere(currentSong)) {
		if (currentSong->isClipActive(clip)) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
			    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);

			clip->reGetParameterAutomation(modelStackWithTimelineCounter);
			clip->expectEvent();
		}
	}
}

// PlaybackMode implementation -----------------------------------------------------------------------

void Session::setupPlayback() {

	currentSong->setParamsInAutomationMode(playbackHandler.recording == RecordingMode::ARRANGEMENT);

	lastSectionArmed = REALLY_OUT_OF_RANGE;
}

// Returns whether to do an instant song swap
bool Session::endPlayback() {
	lastSectionArmed = REALLY_OUT_OF_RANGE;

	bool anyClipsRemoved = currentSong->deletePendingOverdubs();

	for (int32_t c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

		clip->cancelAnyArming();
		if (currentSong->isClipActive(clip)) {
			clip->expectNoFurtherTicks(currentSong);
		}
	}

	currentSong->deactivateAnyArrangementOnlyClips(); // In case any still playing after switch from arrangement

	// If we were waiting for a launch event, we've now stopped so will never reach that point in time, so we'd better
	// swap right now.
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
		// use root UI in case this is called from performance view
		sessionView.requestRendering(getRootUI());

		// And exit RECORD mode, as indicated on LED
		if (playbackHandler.recording == RecordingMode::NORMAL) {
			playbackHandler.recording = RecordingMode::OFF;
			// I guess we're gonna update the LED states sometime soon...
		}
	}
	return false; // No song swap
}

bool Session::wantsToDoTempolessRecord(int32_t newPos) {

	bool mightDoTempolessRecord =
	    (!newPos && playbackHandler.recording == RecordingMode::NORMAL && !playbackHandler.metronomeOn);
	if (!mightDoTempolessRecord) {
		return false;
	}

	bool anyActiveClips = false;

	for (int32_t c = currentSong->sessionClips.getNumElements() - 1; c >= 0; c--) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

		if (currentSong->isClipActive(clip)) {
			anyActiveClips = true;

			if (clip->type != ClipType::AUDIO) {
				return false; // Cos there's a non-audio clip playing or recording
			}

			if (!clip->wantsToBeginLinearRecording(currentSong)) {
				return false;
			}
		}
	}

	if (!anyActiveClips) {
		return false;
	}

	return true;
}

void Session::resetPlayPos(int32_t newPos, bool doingComplete, int32_t buttonPressLatency) {

	// This function may begin a tempoless record - but it doesn't actually know or need to know whether that's the
	// resulting outcome

	AudioEngine::bypassCulling = true;

	// In case any still playing after switch from arrangement. Remember,
	// this function will be called on playback begin, song swap, and more
	currentSong->deactivateAnyArrangementOnlyClips();

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStack);

		currentSong->paramManager.setPlayPos(arrangement.playbackStartedAtPos, modelStackWithThreeMainThings, false);
	}

	int32_t distanceTilLaunchEvent = 0;

	for (int32_t c = currentSong->sessionClips.getNumElements() - 1; c >= 0; c--) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

		// Sometimes, after finishing a tempoless record, a new pending overdub will have been created, and we need to
		// act on it here
		if (clip->isPendingOverdub) {
			clip->activeIfNoSolo = true;
			clip->armState = ArmState::OFF;
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

					if (clip->armState == ArmState::ON_NORMAL) { // Rohan: What's this for again? Auto arming of
						                                         // sections? I think not linear recording...
						distanceTilLaunchEvent = std::max(distanceTilLaunchEvent, clip->loopLength);
					}
				}
			}

			// Rohan: Not quite sure why we needed to set this here?
			clip->output->setActiveClip(modelStackWithTimelineCounter);
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
			armingChanged(); // This isn't really ideal. Is here for AudioClips which just armed themselves in setPos()
			                 // call
		}
	}
}

// TODO: I'd like to have it so this doesn't get called on the first tick of playback - now that this function is also
// responsible for doing the incrementing. It works fine because we supply the increment as 0 in that case, but it'd be
// more meaningful this proposed new way...

// Returns whether Song was swapped
bool Session::considerLaunchEvent(int32_t numTicksBeingIncremented) {

	bool swappedSong = false;
	Clip* nextClipWithFillEvent = nullptr;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	// We now increment the currentPos of all Clips before doing any launch event - so that any new Clips which get
	// launched won't then get their pos incremented

	// For each Clip in session and arranger (we include arrangement-only Clips, which might still be left playing after
	// switching from arrangement to session)
	ClipArray* clipArray = &currentSong->sessionClips;
traverseClips:
	for (int32_t c = 0; c < clipArray->getNumElements(); c++) {
		Clip* clip = clipArray->getClipAtIndex(c);

		if (clip->fillEventAtTickCount > 0) {
			if (!nextClipWithFillEvent || nextClipWithFillEvent->fillEventAtTickCount > clip->fillEventAtTickCount) {
				nextClipWithFillEvent = clip;
			}
		}
		else if (!currentSong->isClipActive(clip)) {
			continue;
		}

		if (clip->output->getActiveClip() && clip->output->getActiveClip()->beingRecordedFromClip == clip) {
			clip = clip->output->getActiveClip();
		}

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

		clip->incrementPos(modelStackWithTimelineCounter, numTicksBeingIncremented);
	}
	if (clipArray != &currentSong->arrangementOnlyClips) {
		clipArray = &currentSong->arrangementOnlyClips;
		goto traverseClips;
	}

	bool enforceSettingUpArming = false;

	if (nextClipWithFillEvent && playbackHandler.lastSwungTickActioned >= nextClipWithFillEvent->fillEventAtTickCount) {
		doLaunch(true);
		armingChanged();
		nextClipWithFillEvent->fillEventAtTickCount = 0;
	}

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
				lastSectionArmed = REALLY_OUT_OF_RANGE;
				playbackHandler.doSongSwap();
				swappedSong = true;

				// If new song has us in arrangement...
				if (currentPlaybackMode == &arrangement) {
					return swappedSong;
				}

				currentPlaybackMode->resetPlayPos(0); // activeClips have been set up already / PGMs have been sent.
				                                      // Calling this will resync arpeggiators though...
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

				// NOTE: we do NOT want to set playbackHandler.swungTicksSkipped to 0 here, cos that'd mess up all the
				// other Clips!

				cancelAllLaunchScheduling();
				doLaunch(false);
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
			if (display->haveOLED()) {
				RootUI* rootUI = getRootUI();
				if (loadSongUI.isLoadingSong()) {
					loadSongUI.displayLoopsRemainingPopup();
				}
				else if ((rootUI == &sessionView || rootUI == &performanceView)
				         && !isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)) {
					renderUIsForOled();
				}
			}
			else {
				sessionView.redrawNumericDisplay();
			}
		}
	}

	// If this is the first tick, we have to do some stuff to arm the first song-section change
	if (playbackHandler.lastSwungTickActioned == 0 || enforceSettingUpArming) {
		int32_t currentSection = userWantsToArmNextSection();

		if (currentSection < SECTION_OUT_OF_RANGE && currentSong->areAllClipsInSectionPlaying(currentSection)) {
			currentSong->sectionToReturnToAfterSongEnd = currentSection;
		}
		else {
			currentSong->sectionToReturnToAfterSongEnd = REALLY_OUT_OF_RANGE;
		}
	}

	return swappedSong;
}

void Session::doTickForward(int32_t posIncrement) {
	// if we're exporting clip stems in song view (e.g. not arrangement tracks)
	// we want to export up to length of the longest sequence in the clip (clip or note row loop length)
	// when we reach longest loop length, we stop playback and allow recording to continue until silence
	if (stemExport.checkForLoopEnd()) [[unlikely]] {
		// if true, then stem export is running, we've already processed the full sequence,
		// and we've stopped playback
		// return as there is nothing else to process
		return;
	}

	if (launchEventAtSwungTickCount) {
		int32_t ticksTilLaunchEvent = launchEventAtSwungTickCount - playbackHandler.lastSwungTickActioned;

		playbackHandler.swungTicksTilNextEvent = std::min(ticksTilLaunchEvent, playbackHandler.swungTicksTilNextEvent);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings = currentSong->addToModelStack(modelStack);

		if (currentSong->paramManager.mightContainAutomation()) {
			currentSong->paramManager.processCurrentPos(modelStackWithThreeMainThings, posIncrement, false);
			playbackHandler.swungTicksTilNextEvent =
			    std::min(playbackHandler.swungTicksTilNextEvent, currentSong->paramManager.ticksTilNextEvent);
		}
	}

	// Tell all the Clips that it's tick time. Including arrangement-only Clips, which might still be left playing after
	// switching from arrangement to session For each Clip in session and arranger
	ClipArray* clipArray;

	for (uint8_t iPass = 0; iPass < 4; iPass++) {

		if (iPass % 2 == 0) {
			clipArray = &currentSong->sessionClips;
		}
		else {
			clipArray = &currentSong->arrangementOnlyClips;
		}

		for (int32_t c = 0; c < clipArray->getNumElements(); c++) {
			Clip* clip = clipArray->getClipAtIndex(c);
			if (!(clip->output)) {
				// possible while swapping songs and render is called between deallocating the output and its clips
				continue;
			}
			if (clip->output->needsEarlyPlayback() == (iPass > 1)) {
				continue; // 1st/2nd time through, skip anything but priority clips, which must take effect first
				          // 3rd/4th time through, skip the priority clips already actioned.
			}

			if (clip->fillEventAtTickCount > 0) {
				int32_t ticksTilNextFillEvent = clip->fillEventAtTickCount - playbackHandler.lastSwungTickActioned;
				playbackHandler.swungTicksTilNextEvent =
				    std::min(ticksTilNextFillEvent, playbackHandler.swungTicksTilNextEvent);
			}

			if (!currentSong->isClipActive(clip)) {
				continue;
			}

			if (clip->output->getActiveClip() && clip->output->getActiveClip()->beingRecordedFromClip == clip) {
				clip = clip->output->getActiveClip();
			}

			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			// No need to do the actual incrementing - that's been done for all Clips (except ones which have only just
			// launched), up in considerLaunchEvent()

			// May create new Clip and put it in the ModelStack - we'll check below.
			clip->processCurrentPos(modelStackWithTimelineCounter, posIncrement);

			// NOTE: posIncrement is the number of ticks which we incremented by in considerLaunchEvent(). But for Clips
			// which were only just launched in there, well the won't have been incremented, so it would be more correct
			// if posIncrement were 0 here. But, I don't believe there's any ill-effect from having a posIncrement too
			// big in this case. It's just not super elegant.

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
	}

	// Do arps too (hmmm, could we want to do this in considerLaunchEvent() instead, just like the incrementing?)
	for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {

		int32_t posForArp;
		if (thisOutput->getActiveClip() && currentSong->isClipActive(thisOutput->getActiveClip())) {
			posForArp = thisOutput->getActiveClip()->lastProcessedPos;
		}
		else {
			posForArp = playbackHandler.lastSwungTickActioned;
		}

		int32_t ticksTilNextArpEvent = thisOutput->doTickForwardForArp(modelStack, posForArp);
		playbackHandler.swungTicksTilNextEvent = std::min(ticksTilNextArpEvent, playbackHandler.swungTicksTilNextEvent);
	}

	// Do sequencer modes too - similar to arpeggiator handling
	for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {
		if (thisOutput->getActiveClip() && currentSong->isClipActive(thisOutput->getActiveClip())) {
			Clip* activeClip = thisOutput->getActiveClip();
			if (activeClip->type == ClipType::INSTRUMENT) {
				InstrumentClip* instrumentClip = static_cast<InstrumentClip*>(activeClip);
				if (instrumentClip->hasSequencerMode()) {
					ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
					    modelStack->addTimelineCounter(instrumentClip);
					auto* sequencerMode = instrumentClip->getSequencerMode();
					if (sequencerMode) {
						int32_t ticksTilNextSequencerEvent = sequencerMode->processPlayback(
						    modelStackWithTimelineCounter, playbackHandler.lastSwungTickActioned);
						if (ticksTilNextSequencerEvent > 0) {
							playbackHandler.swungTicksTilNextEvent =
							    std::min(ticksTilNextSequencerEvent, playbackHandler.swungTicksTilNextEvent);
						}
					}
				}
			}
		}
	}

	/*
	for (Instrument* thisInstrument = currentSong->firstInstrument; thisInstrument; thisInstrument =
	thisInstrument->next) { if (thisInstrument->activeClip && isClipActive(thisInstrument->activeClip)) {
	        thisInstrument->activeClip->doTickForward(posIncrement);
	    }
	}
	*/
}

void Session::resyncToSongTicks(Song* song) {

	for (int32_t c = 0; c < song->sessionClips.getNumElements(); c++) {
		Clip* clip = song->sessionClips.getClipAtIndex(c);

		if (song->isClipActive(clip)) {
			reSyncClipToSongTicks(clip);
		}
	}
}

void Session::unsoloClip(Clip* clip) {
	clip->soloingInSessionMode = false;

	currentSong->reassessWhetherAnyClipsSoloing();

	if (!hasPlaybackActive()) {
		return;
	}

	bool anyClipsStillSoloing = currentSong->getAnyClipsSoloing();

	// If any other Clips are still soloing, or this Clip isn't active outside of solo mode, we need to shut that Clip
	// up
	if (anyClipsStillSoloing || !clip->activeIfNoSolo) {
		clip->expectNoFurtherTicks(currentSong);

		if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
			clip->getClipToRecordTo()->endInstance(playbackHandler.getActualArrangementRecordPos());
		}
	}

	// Re-activate *other* Clips (i.e. not this one) if this was the only soloed Clip
	if (!anyClipsStillSoloing) {

		int32_t modifiedStartPos = (int32_t)clip->lastProcessedPos; // - swungTicksSkipped;
		if (modifiedStartPos < 0) {
			modifiedStartPos += clip->loopLength;
		}

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

		for (int32_t c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
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
			for (int32_t c = 0; c < modelStack->song->sessionClips.getNumElements(); c++) {
				Clip* thisClip = modelStack->song->sessionClips.getClipAtIndex(c);
				if (thisClip != clip) { // Only *other* Clips
					if (thisClip->activeIfNoSolo) {
						thisClip->expectNoFurtherTicks(modelStack->song);

						if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
							thisClip->getClipToRecordTo()->endInstance(playbackHandler.getActualArrangementRecordPos());
						}
					}

					// As noted above, non-solo arming is not allowed now that there will be a Clip soloing.
					if (thisClip->armState == ArmState::ON_NORMAL) {
						thisClip->armState = ArmState::OFF;
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
	if (!playbackHandler.playbackState || !output->getActiveClip()) {
		return true;
	}

	return !currentSong->doesOutputHaveActiveClipInSession(output);
}

void Session::stopOutputRecordingAtLoopEnd() {
	// If no launch-event currently, plan one
	if (!launchEventAtSwungTickCount) {
		armAllClipsToStop(1);
		lastSectionArmed = SECTION_OUT_OF_RANGE;
		armingChanged();
	}

	playbackHandler.stopOutputRecordingAtLoopEnd = true;
}

int32_t Session::getPosAtWhichClipWillCut(ModelStackWithTimelineCounter const* modelStack) {

	Clip* clip = (Clip*)modelStack->getTimelineCounter();

	// If recording arrangement, pretend it's gonna cut at the end of the current length, cos we're actually going to
	// auto-extend it when we get there, so we don't want any wrapping-around happening
	if (clip->isArrangementOnlyClip() && playbackHandler.recording == RecordingMode::ARRANGEMENT
	    && clip->beingRecordedFromClip) {
		return clip->currentlyPlayingReversed ? 0 : clip->loopLength;
	}

	int32_t cutPos;

	if (willClipContinuePlayingAtEnd(modelStack)) {
		cutPos = clip->currentlyPlayingReversed ? (-2147483648) : 2147483647; // If it's gonna loop, it's not gonna cut
	}

	else {
		int32_t ticksTilLaunchEvent = launchEventAtSwungTickCount - playbackHandler.lastSwungTickActioned;
		if (clip->currentlyPlayingReversed) {
			ticksTilLaunchEvent = -ticksTilLaunchEvent;
		}
		cutPos = clip->lastProcessedPos + ticksTilLaunchEvent;
	}

	// If pingponging, that's actually going to get referred to as a cut.
	if (clip->sequenceDirectionMode == SequenceDirection::PINGPONG) {
		if (clip->currentlyPlayingReversed) {
			if (cutPos < 0) {
				cutPos = clip->lastProcessedPos
				             ? 0
				             : -clip->loopLength; // Check we're not right at pos 0, as we briefly will be when we
				                                  // pingpong at the right-hand end of the Clip/etc.
			}
		}
		else {
			if (cutPos > clip->loopLength) {
				cutPos = clip->loopLength;
			}
		}
	}

	return cutPos;
}

bool Session::willClipContinuePlayingAtEnd(ModelStackWithTimelineCounter const* modelStack) {

	Clip* clip = (Clip*)modelStack->getTimelineCounter();

	if (!modelStack->song->isClipActive(clip)) {
		return false; // If Clip not active, just say it won't loop. We need that, cos an AudioClip's Sample may
	}
	// keep playing just after its Clip has stopped, and we don't wanna think it needs to loop

	// Note: this isn't quite perfect - it doesn’t know if Clip will cut out due to another one launching. But the ill
	// effects of this are pretty minor.
	bool willLoop =
	    !launchEventAtSwungTickCount             // If no launch event scheduled, obviously it'll loop
	    || numRepeatsTilLaunch > 1               // If the launch event is gonna just trigger another repeat, it'll loop
	    || clip->armState != ArmState::ON_NORMAL // If not armed, or armed to solo, it'll loop (except see above)
	    || (clip->soloingInSessionMode
	        && clip->activeIfNoSolo); // We know from the previous test that clip is armed. If it's soloing, that means
	                                  // it's armed to stop soloing. And if it is activeIfNoSolo, that means it'll keep
	                                  // playing, if we assume *all* clips are going to stop soloing (a false positive
	                                  // here doesn't matter too much)

	// Ok, that's most of our tests done. If one of them gave a true, we can get out now.
	if (willLoop) {
		return true;
	}

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
	    clip->activeIfNoSolo; // Yep, this works better (in some complex scenarios I tested) than calling
	                          // isClipActive(), which would take soloing into account

	if (shouldBeActiveWhileExistent && !(playbackHandler.playbackState && currentPlaybackMode == &arrangement)) {
		int32_t newClipIndex;
		Clip* newClip = currentSong->getSessionClipWithOutput(clip->output, -1, clip, &newClipIndex, true);
		if (newClip) {
			toggleClipStatus(newClip, &newClipIndex, true, 0);
		}
	}

	return shouldBeActiveWhileExistent;
}
