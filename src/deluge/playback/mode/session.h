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

#pragma once
#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "playback/mode/playback_mode.h"

class InstrumentClip;
class Clip;
class ModelStackWithTimelineCounter;
class ModelStack;
enum class LaunchStatus;

#define SECTION_OUT_OF_RANGE 254
#define REALLY_OUT_OF_RANGE 255
#define LAUNCH_EXCLUSIVE -2
#define LAUNCH_NON_EXCLUSIVE -1
#define LAUNCH_REPEAT_INFINITELY 0
#define LAUNCH_REPEAT_ONCE 1

class Session final : public PlaybackMode {
public:
	Session();

	void armAllClipsToStop(int32_t afterNumRepeats);
	void armNextSection(int32_t oldSection, int32_t numRepetitions = -1);
	void doLaunch(bool isFillLaunch);
	void scheduleLaunchTiming(int64_t atTickCount, int32_t numRepeatsUntil, int32_t armedLaunchLengthForOneRepeat);
	int32_t getNumSixteenthNotesRemainingTilLaunch();
	void scheduleFillEvent(Clip* clip, int64_t atTickCount);
	void cancelAllLaunchScheduling();
	void launchSchedulingMightNeedCancelling();
	void reSyncClipToSongTicks(Clip* clip);
	void reSyncClip(ModelStackWithTimelineCounter* modelStack, bool mustSetPosToSomething = false,
	                bool mayResumeClip = true);
	void userWantsToUnsoloClip(Clip* clip, bool forceLateStart, int32_t buttonPressLatency);
	void toggleClipStatus(Clip* clip, int32_t* clipIndex, bool doInstant, int32_t buttonPressLatency);
	void soloClipAction(Clip* clip, int32_t buttonPressLatency);
	void armSection(uint8_t section, int32_t buttonPressLatency);
	void armingChanged();
	void userWantsToArmClipsToStartOrSolo(uint8_t section, Clip* clip, bool stopAllOtherClips,
	                                      bool forceLateStart = false, bool allowLateStart = true,
	                                      int32_t numRepeatsTilLaunch = 1, bool allowSubdividedQuantization = true,
	                                      ArmState armState = ArmState::ON_NORMAL);
	LaunchStatus investigateSyncedLaunch(Clip* waitForClip, uint32_t* currentPosWithinQuantization,
	                                     uint32_t* quantization, uint32_t longestStartingClipLength,
	                                     bool allowSubdividedQuantization);
	bool armForSongSwap();
	bool armForSwitchToArrangement();
	void armClipsToStartOrSoloWithQuantization(uint32_t pos, uint32_t quantization, uint8_t section,
	                                           bool stopAllOtherClips, Clip* clip, bool forceLateStart,
	                                           bool allowLateStart, int32_t numRepeatsTilLaunch,
	                                           ArmState armState = ArmState::ON_NORMAL);
	void armClipToStartOrSoloUsingQuantization(Clip* thisClip, bool doLateStart, uint32_t pos,
	                                           ArmState armState = ArmState::ON_NORMAL,
	                                           bool mustUnarmOtherClipsWithSameOutput = true);
	void cancelAllArming();
	void armClipLowLevel(Clip* loopableToArm, ArmState armState, bool mustUnarmOtherClipsWithSameOutput = true);
	int32_t userWantsToArmNextSection(int32_t numRepetitions = -1);
	int32_t getCurrentSection();
	bool areAnyClipsArmed();
	void unsoloClip(Clip* clip);
	void soloClipRightNow(ModelStackWithTimelineCounter* modelStack);
	bool deletingClipWhichCouldBeAbandonedOverdub(Clip* clip);
	void scheduleOverdubToStartRecording(Clip* overdub, Clip* clipAbove);
	void justAbortedSomeLinearRecording();

	// PlaybackMode implementation
	void setupPlayback();
	bool endPlayback(); // Returns whether to do an instant song swap
	void doTickForward(int32_t posIncrement);
	void resetPlayPos(int32_t newPos, bool doingComplete = true, int32_t buttonPressLatency = 0);
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
	bool giveClipOpportunityToBeginLinearRecording(Clip* clip, int32_t clipIndex, int32_t buttonPressLatency);
	void armClipToStopAction(Clip* clip);
	void cancelArmingForClip(Clip* clip, int32_t* clipIndex);
	void armSectionWhenNeitherClockActive(ModelStack* modelStack, int32_t section, bool stopAllOtherClips);
	void armClipsAlongWithExistingLaunching(ArmState armState, uint8_t section, Clip* clip);
	void armClipsWithNothingToSyncTo(uint8_t section, Clip* clip);
	void scheduleFillClip(Clip* clip);
	void scheduleFillClips(uint8_t section);
};

extern Session session;
extern const deluge::gui::colours::Colour defaultClipSectionColours[];
