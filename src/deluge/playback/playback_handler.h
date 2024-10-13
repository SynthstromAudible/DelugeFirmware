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
#include "util/d_string.h"
#include <cstdint>

enum class RecordingMode {
	OFF,
	NORMAL,
	ARRANGEMENT,
};

constexpr int32_t kNumInputTicksForMovingAverage = 24;

#define PLAYBACK_CLOCK_INTERNAL_ACTIVE 1
#define PLAYBACK_CLOCK_EXTERNAL_ACTIVE 2
#define PLAYBACK_SWITCHED_ON 4

#define PLAYBACK_CLOCK_EITHER_ACTIVE (PLAYBACK_CLOCK_INTERNAL_ACTIVE | PLAYBACK_CLOCK_EXTERNAL_ACTIVE)

class Song;
class InstrumentClip;
class NoteRow;
class Kit;
class Clip;
class Action;
class MIDIDevice;

constexpr uint16_t metronomeValuesBPM[16] = {
    60, 63, 66, 69, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116,
};

constexpr uint16_t metronomeValueBoundaries[16] = {
    1793, 1709, 1633, 1542, 1490, 1413, 1345, 1282, 1225, 1173, 1125, 1081, 1040, 1002, 967, 934,
};

class PlaybackHandler {
public:
	PlaybackHandler();
	void routine();

	void playButtonPressed(int32_t buttonPressLatency);
	void recordButtonPressed();
	void setupPlaybackUsingInternalClock(int32_t buttonPressLatencyForTempolessRecord = 0, bool allowCountIn = true,
	                                     bool restartingPlayback = false);
	void setupPlaybackUsingExternalClock(bool switchingFromInternalClock = false, bool fromContinueCommand = false);
	void setupPlayback(int32_t newPlaybackState, int32_t playFromPos, bool doOneLastAudioRoutineCall = false,
	                   bool shouldShiftAccordingToClipInstance = true,
	                   int32_t buttonPressLatencyForTempolessRecord = 0);
	void endPlayback();
	void inputTick(bool fromTriggerClock = false, uint32_t time = 0);
	void startMessageReceived();
	void continueMessageReceived();
	void stopMessageReceived();
	void clockMessageReceived(uint32_t time);
	void tempoEncoderAction(int8_t offset, bool encoderButtonPressed, bool shiftButtonPressed);
	bool isCurrentlyRecording();
	void positionPointerReceived(uint8_t data1, uint8_t data2);
	void doSongSwap(bool preservePlayPosition = false);
	void forceResetPlayPos(Song* song, bool restartingPlayback = false);
	void expectEvent();
	void setMidiInClockEnabled(bool newValue);
	int32_t getActualArrangementRecordPos();
	int32_t getArrangementRecordPosAtLastActionedSwungTick();
	void slowRoutine();
	void scheduleSwungTickFromExternalClock();
	int32_t getNumSwungTicksInSinceLastTimerTick(uint32_t* timeRemainder = NULL);
	int32_t getNumSwungTicksInSinceLastActionedSwungTick(uint32_t* timeRemainder = NULL);
	int64_t getActualSwungTickCount(uint32_t* timeRemainder = NULL);
	int64_t getCurrentInternalTickCount(uint32_t* remainder = NULL);
	void scheduleSwungTick();
	int32_t getInternalTickTime(int64_t internalTickCount);
	void scheduleTriggerClockOutTick();
	void scheduleMIDIClockOutTick();
	void scheduleNextTimerTick(uint32_t doubleSwingInterval);

	// MIDI-clock-out ticks
	bool midiClockOutTickScheduled;
	uint32_t timeNextMIDIClockOutTick;
	int64_t lastMIDIClockOutTickDone;

	// Playback
	uint8_t playbackState;
	bool usingAnalogClockInput; // Value is only valid if usingInternalClock is false
	RecordingMode recording;
	bool ignoringMidiClockInput;

	int32_t posToNextContinuePlaybackFrom; // This will then have 1 subtracted from it when actually physically set
	uint32_t timeLastMIDIStartOrContinueMessageSent;

	// Timer ticks
	int64_t lastTimerTickActioned;  // Not valid while playback being set up
	int64_t nextTimerTickScheduled; // *Yes* valid while (internal clock) playback being set up. Will be zero during
	                                // that time
	uint64_t timeNextTimerTickBig;  // Not valid while playback being set up
	uint64_t timeLastTimerTickBig;  // Not valid while playback being set up

	// Input ticks
	uint32_t timeLastInputTicks[kNumInputTicksForMovingAverage];
	uint32_t timePerInputTickMovingAverage; // 0 means that a default will be set the first time it's used
	uint8_t numInputTickTimesCounted;

	bool tempoMagnitudeMatchingActiveNow;
	// unsigned long timeFirstInputTick; // First tick received for current tally
	unsigned long
	    timeVeryFirstInputTick; // Very first tick received in playback. Only used for tempo magnitude matching
	int64_t lastInputTickReceived;
	unsigned long targetedTimePerInputTick;

	// Swung ticks
	bool swungTickScheduled;
	uint32_t scheduledSwungTickTime;
	// Now, swung ticks are only "actioned" in the following circumstances:
	// - A note starts or ends
	// - Automation event
	// - A clip loops
	// - A "launch" event
	// - Start of playback, including if count-in ends
	int64_t lastSwungTickActioned; // Will be set to a phony-ish "0" while playback being set up

	// Trigger-clock-out ticks
	bool triggerClockOutTickScheduled;
	uint32_t timeNextTriggerClockOutTick;
	int64_t lastTriggerClockOutTickDone;

	uint32_t analogOutTicksPPQN; // Relative to the "external world", which also means relative to the displayed tempo
	uint32_t analogInTicksPPQN;
	uint32_t timeLastAnalogClockInputRisingEdge;
	bool analogClockInputAutoStart;

	bool songSwapShouldPreserveTempo;

	// User options
	bool metronomeOn;
	bool midiOutClockEnabled;
	bool midiInClockEnabled;
	bool tempoMagnitudeMatchingEnabled;
	uint8_t countInBars;

	int32_t swungTicksTilNextEvent;

	int32_t ticksLeftInCountIn;
	int32_t currentVisualCountForCountIn;

	int32_t metronomeOffset;

	void setLedStates();
	void tapTempoAutoSwitchOff();
	void reassessInputTickScaling();
	void resyncInternalTicksToInputTicks(Song* song);
	bool shouldRecordNotesNow();
	void stopAnyRecording();
	uint32_t getTimePerInternalTick();
	uint64_t getTimePerInternalTickBig();
	float getTimePerInternalTickFloat();
	uint32_t getTimePerInternalTickInverse(bool getStickyValue = false);
	void tapTempoButtonPress();
	void doTriggerClockOutTick();
	void doMIDIClockOutTick();
	void resyncAnalogOutTicksToInternalTicks();
	void resyncMIDIClockOutTicksToInternalTicks();
	void analogClockRisingEdge(uint32_t time);
	void toggleMetronomeStatus();
	void commandDisplayTempo();
	void setMidiOutClockMode(bool newValue);
	void pitchBendReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2, bool* doingMidiThru);
	void midiCCReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru);
	void programChangeReceived(MIDIDevice* device, int32_t channel, int32_t program);
	void aftertouchReceived(MIDIDevice* fromDevice, int32_t channel, int32_t value, int32_t noteCode,
	                        bool* doingMidiThru); // noteCode -1 means channel-wide
	void loopCommand(OverDubType overdubNature);
	void grabTempoFromClip(Clip* clip);
	int32_t getTimeLeftInCountIn();

	void noteMessageReceived(MIDIDevice* fromDevice, bool on, int32_t channel, int32_t note, int32_t velocity,
	                         bool* doingMidiThru);
	bool subModeAllowsRecording();

	float calculateBPM(float timePerInternalTick);
	void switchToArrangement();
	void switchToSession();
	void finishTempolessRecording(bool startPlaybackAgain, int32_t buttonLatencyForTempolessRecord,
	                              bool shouldExitRecordMode = true);

	int32_t arrangementPosToStartAtOnSwitch;

	bool stopOutputRecordingAtLoopEnd;

	void actionTimerTick();
	void actionTimerTickPart2();
	void actionSwungTick();
	void scheduleSwungTickFromInternalClock();
	bool currentlySendingMIDIOutputClocks();

	inline bool isExternalClockActive() { return (playbackState & PLAYBACK_CLOCK_EXTERNAL_ACTIVE); }
	inline bool isInternalClockActive() { return (playbackState & PLAYBACK_CLOCK_INTERNAL_ACTIVE); }
	inline bool isEitherClockActive() { return (playbackState & PLAYBACK_CLOCK_EITHER_ACTIVE); }

	// TEMPO encoder commands
	void commandDisplaySwingAmount();
	void commandEditSwingAmount(int8_t offset);
	void commandDisplaySwingInterval();
	void commandEditSwingInterval(int8_t offset);
	void commandNudgeClock(int8_t offset);
	void commandEditClockOutScale(int8_t offset);
	void commandEditTempoCoarse(int8_t offset);
	void commandEditTempoFine(int8_t offset);
	void commandDisplayTempo(int8_t offset);
	void commandClearTempoAutomation();

	void getTempoStringForOLED(float tempoBPM, StringBuf& buffer);

	void tryLoopCommand(GlobalMIDICommand command);

	float calculateBPMForDisplay();

private:
	uint32_t timerTicksToOutputTicks(uint32_t timerTicks);

	// These are all only relevant while playing synced.
	uint32_t timePerInternalTickMovingAverage;        // Recalculated every ~24 clocks
	uint32_t veryCurrentTimePerInternalTickInverse;   // Recalculated at every received clock message
	uint32_t stickyCurrentTimePerInternalTickInverse; // Moving average kinda thing
	uint32_t lowpassedTimePerInternalTick;
	uint32_t slowpassedTimePerInternalTick;
	uint32_t stickyTimePerInternalTick;

	uint16_t tapTempoNumPresses;
	uint32_t tapTempoFirstPressTime;

	int32_t numOutputClocksWaitingToBeSent;
	int32_t numInputTicksToSkip;
	uint32_t skipAnalogClocks;
	uint32_t skipMidiClocks;

	void resetTimePerInternalTickMovingAverage();
	void getCurrentTempoParams(int32_t* magnitude, int8_t* whichValue);
	void displayTempoFromParams(int32_t magnitude, int8_t whichValue);
	void displayTempoBPM(float tempoBPM);
	void getAnalogOutTicksToInternalTicksRatio(uint32_t* internalTicksPer, uint32_t* analogOutTicksPer);
	void getMIDIClockOutTicksToInternalTicksRatio(uint32_t* internalTicksPer, uint32_t* midiClockOutTicksPer);
	void getInternalTicksToInputTicksRatio(uint32_t* inputTicksPer, uint32_t* internalTicksPer);
	void sendOutPositionViaMIDI(int32_t pos, bool outputClocksWereSwitchedOff = false);
	// void scheduleNextTimerTick();
	bool startIgnoringMidiClockInputIfNecessary();
	uint32_t setTempoFromAudioClipLength(uint64_t loopLengthSamples, Action* action);
	bool offerNoteToLearnedThings(MIDIDevice* fromDevice, bool on, int32_t channel, int32_t note);
	bool tryGlobalMIDICommands(MIDIDevice* device, int32_t channel, int32_t note);
	bool tryGlobalMIDICommandsOff(MIDIDevice* device, int32_t channel, int32_t note);
	void decideOnCurrentPlaybackMode();
	float getCurrentInternalTickFloatFollowingExternalClock();
	void scheduleTriggerClockOutTickParamsKnown(uint32_t analogOutTicksPer, uint64_t fractionLastTimerTick,
	                                            uint64_t fractionNextAnalogOutTick);
	void scheduleMIDIClockOutTickParamsKnown(uint32_t midiClockOutTicksPer, uint64_t fractionLastTimerTick,
	                                         uint64_t fractionNextMIDIClockOutTick);
};

extern PlaybackHandler playbackHandler;
