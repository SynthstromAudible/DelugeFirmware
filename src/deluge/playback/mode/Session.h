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

#ifndef SESSION_H_
#define SESSION_H_
#include "PlaybackMode.h"
#include "definitions.h"

class InstrumentClip;
class Clip;
class ModelStackWithTimelineCounter;

class Session final : public PlaybackMode {
public:
	Session();

	void armAllClipsToStop(int afterNumRepeats);
	void armNextSection(int oldSection, int numRepetitions = -1);
	void doLaunch();
	void scheduleLaunchTiming(int64_t atTickCount, int numRepeatsUntil, int32_t armedLaunchLengthForOneRepeat);
	void cancelAllLaunchScheduling();
	void launchSchedulingMightNeedCancelling();
	void reSyncClipToSongTicks(Clip* clip);
	void reSyncClip(ModelStackWithTimelineCounter* modelStack, bool mustSetPosToSomething = false,
	                bool mayResumeClip = true);
	void userWantsToUnsoloClip(Clip* clip, bool forceLateStart, int buttonPressLatency);
	void toggleClipStatus(Clip* clip, int* clipIndex, bool doInstant, int buttonPressLatency);
	void soloClipAction(Clip* clip, int buttonPressLatency);
	void armSection(uint8_t section, int buttonPressLatency);
	void armingChanged();
	void userWantsToArmClipsToStartOrSolo(uint8_t section, Clip* clip, bool stopAllOtherClips,
	                                      bool forceLateStart = false, bool allowLateStart = true,
	                                      int numRepeatsTilLaunch = 1, bool allowSubdividedQuantization = true,
	                                      int armState = ARM_STATE_ON_NORMAL);
	int investigateSyncedLaunch(Clip* waitForClip, uint32_t* currentPosWithinQuantization, uint32_t* quantization,
	                            uint32_t longestStartingClipLength, bool allowSubdividedQuantization);
	bool armForSongSwap();
	bool armForSwitchToArrangement();
	void armClipsToStartOrSoloWithQuantization(uint32_t pos, uint32_t quantization, uint8_t section,
	                                           bool stopAllOtherClips, Clip* clip, bool forceLateStart,
	                                           bool allowLateStart, int numRepeatsTilLaunch,
	                                           int armState = ARM_STATE_ON_NORMAL);
	void armClipToStartOrSoloUsingQuantization(Clip* thisClip, bool doLateStart, uint32_t pos,
	                                           int armState = ARM_STATE_ON_NORMAL,
	                                           bool mustUnarmOtherClipsWithSameOutput = true);
	void cancelAllArming();
	void armClipLowLevel(Clip* loopableToArm, int armState, bool mustUnarmOtherClipsWithSameOutput = true);
	int userWantsToArmNextSection(int numRepetitions = -1);
	int getCurrentSection();
	bool areAnyClipsArmed();
	void unsoloClip(Clip* clip);
	void soloClipRightNow(ModelStackWithTimelineCounter* modelStack);
	bool deletingClipWhichCouldBeAbandonedOverdub(Clip* clip);
	void scheduleOverdubToStartRecording(Clip* overdub, Clip* clipAbove);
	void justAbortedSomeLinearRecording();

	// PlaybackMode implementation
	void setupPlayback();
	bool endPlayback(); // Returns whether to do an instant song swap
	void doTickForward(int posIncrement);
	void resetPlayPos(int32_t newPos, bool doingComplete = true, int buttonPressLatency = 0);
	void resyncToSongTicks(Song* song);
	void reversionDone();
	bool isOutputAvailable(Output* output);
	bool considerLaunchEvent(int32_t numTicksBeingIncremented); // Returns whether Song was swapped
	void stopOutputRecordingAtLoopEnd();
	int32_t getPosAtWhichClipWillCut(ModelStackWithTimelineCounter const* modelStack);
	bool willClipContinuePlayingAtEnd(ModelStackWithTimelineCounter const* modelStack);
	bool willClipLoopAtSomePoint(ModelStackWithTimelineCounter const* modelStack);
	bool wantsToDoTempolessRecord(int32_t newPos);

	uint8_t lastSectionArmed; // 255 means none. 254 means the action was switch-off-all-sections
	uint32_t timeLastSectionPlayed;

	int64_t launchEventAtSwungTickCount;
	int16_t numRepeatsTilLaunch;
	int32_t currentArmedLaunchLengthForOneRepeat;
	bool switchToArrangementAtLaunchEvent;

private:
	bool giveClipOpportunityToBeginLinearRecording(Clip* clip, int clipIndex, int buttonPressLatency);
	void armClipToStopAction(Clip* clip);
	void cancelArmingForClip(Clip* clip, int* clipIndex);
	void armSectionWhenNeitherClockActive(ModelStack* modelStack, int section, bool stopAllOtherClips);
};

extern Session session;

#endif /* SESSION_H_ */
