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

#include "playback/playback_handler.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/menu_item/sync_level.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_session_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/log.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "io/midi/midi_transpose.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/consequence/consequence.h"
#include "model/consequence/consequence_begin_playback.h"
#include "model/consequence/consequence_tempo_change.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/model_stack.h"
#include "model/sample/sample_holder.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "model/sync.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "processing/audio_output.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/metronome/metronome.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/stem_export/stem_export.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/flash_storage.h"
#include "storage/storage_manager.h"
#include "util/cfunctions.h"
#include "util/functions.h"
#include <math.h>
#include <new>

extern "C" {
#include "RZA1/gpio/gpio.h"
#include "RZA1/uart/sio_char.h"
}

PlaybackHandler playbackHandler{};

extern void songLoaded(Song* song);

#define slowpassedTimePerInternalTickSlowness 8

GlobalMIDICommand pendingGlobalMIDICommand = GlobalMIDICommand::NONE; // -1 means none
int32_t pendingGlobalMIDICommandNumClustersWritten;
extern uint8_t currentlyAccessingCard;

// FineTempoKnob variable
int32_t tempoKnobMode = 1;

bool currentlyActioningSwungTickOrResettingPlayPos = false;

PlaybackHandler::PlaybackHandler() {
	tapTempoNumPresses = 0;
	playbackState = 0;
	analogInTicksPPQN = 24;
	analogOutTicksPPQN = 24;
	analogClockInputAutoStart = true;
	metronomeOn = false;
	midiOutClockEnabled = true;
	midiInClockEnabled = true;
	tempoMagnitudeMatchingEnabled = false;
	posToNextContinuePlaybackFrom = 0;
	stopOutputRecordingAtLoopEnd = false;
	recording = RecordingMode::OFF;
	countInBars = 1;
	timeLastMIDIStartOrContinueMessageSent = 0;
	currentVisualCountForCountIn = 0;
	skipAnalogClocks = 0;
	skipMidiClocks = 0;
}

extern "C" uint32_t triggerClockRisingEdgeTimes[];
extern "C" uint32_t triggerClockRisingEdgesReceived;
extern "C" uint32_t triggerClockRisingEdgesProcessed;

// This function will be called repeatedly, at all times, to see if it's time to do a tick, and such
void PlaybackHandler::routine() {

	// Check incoming USB MIDI
	midiEngine.checkIncomingUsbMidi();

	// Check incoming Serial MIDI
	for (int32_t i = 0; i < 12 && midiEngine.checkIncomingSerialMidi(); i++) {
		;
	}

	// Check analog clock input
	if (triggerClockRisingEdgesProcessed != triggerClockRisingEdgesReceived) {
		uint32_t time =
		    triggerClockRisingEdgeTimes[triggerClockRisingEdgesProcessed & (TRIGGER_CLOCK_INPUT_NUM_TIMES_STORED - 1)];
		triggerClockRisingEdgesProcessed++;
		analogClockRisingEdge(time);
	}

	// If we're playing synced to analog clock input, auto-start mode, and there hasn't been a rising edge for a while,
	// then stop
	if (isExternalClockActive() && usingAnalogClockInput && analogClockInputAutoStart
	    && (int32_t)(AudioEngine::audioSampleTimer - timeLastAnalogClockInputRisingEdge) > (kSampleRate >> 1)) {
		endPlayback();
	}
}

void PlaybackHandler::slowRoutine() {
	// See if any MIDI commands are pending which couldn't be actioned before (see comments in tryGlobalMIDICommands())
	if (pendingGlobalMIDICommand != GlobalMIDICommand::NONE && !currentlyAccessingCard) {

		D_PRINTLN("actioning pending command -----------------------------------------");

		if (actionLogger.allowedToDoReversion()) {

			switch (pendingGlobalMIDICommand) {
			case GlobalMIDICommand::UNDO:
				actionLogger.undo();
				break;

			case GlobalMIDICommand::REDO:
				actionLogger.redo();
				break;
			}

			if (ALPHA_OR_BETA_VERSION && pendingGlobalMIDICommandNumClustersWritten) {
				char buffer[12];
				intToString(pendingGlobalMIDICommandNumClustersWritten, buffer);
				display->displayPopup(buffer);
			}
		}

		pendingGlobalMIDICommand = GlobalMIDICommand::NONE;
	}
}

void PlaybackHandler::playButtonPressed(int32_t buttonPressLatency) {

	// If not currently playing
	if (!playbackState) {
		setupPlaybackUsingInternalClock(buttonPressLatency);
		D_PRINTLN("Play");
	}

	// Or if currently playing...
	else {
		D_PRINTLN("~Play");

		bool accessibility = FlashStorage::accessibilityShortcuts;

		RootUI* rootUI = getRootUI();

		bool isArrangerView =
		    rootUI == &arrangerView
		    || (rootUI == &performanceSessionView && currentSong->lastClipInstanceEnteredStartPos != -1)
		    || (rootUI == &automationView && automationView.onArrangerView);

		bool isRestartShortcutPressed =
		    (accessibility && Buttons::isButtonPressed(deluge::hid::button::CROSS_SCREEN_EDIT))
		    || (!accessibility && Buttons::isButtonPressed(deluge::hid::button::X_ENC));

		bool isArrangementPadPressed = isArrangerView && isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW);
		// placeholders:
		// isSongPadPressed = restart playhead in song view
		// isClipPadPressed = restart playhead in clip view

		bool isSequencerPadPressed = isArrangementPadPressed; // add isSongPadPressed and isClipPadPressed here

		// If holding restart shortcut down...
		// or holding pad in arranger view (and eventually song and clip views)
		if (isRestartShortcutPressed || isSequencerPadPressed) {
			// If wanting to switch into arranger...
			if (currentPlaybackMode == &session && isArrangerView) {
				if (isArrangementPadPressed) {
					arrangementPosToStartAtOnSwitch = arrangerView.lastInteractedArrangementPos;
				}
				else {
					arrangementPosToStartAtOnSwitch = currentSong->xScroll[NAVIGATION_ARRANGEMENT];
				}
				session.armForSwitchToArrangement();
				if (display->haveOLED()) {
					renderUIsForOled();
				}
				else {
					sessionView.redrawNumericDisplay();
				}
				display->cancelPopup();
			}

			// Otherwise, if internal clock, restart playback
			else {
				if (isInternalClockActive() && recording != RecordingMode::ARRANGEMENT) {
					forceResetPlayPos(currentSong);
				}
				else {
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_FOLLOWING_EXTERNAL_CLOCK));
				}
			}
		}

		// Or, normal
		else {

			// If playing synced to analog clock input and it's on auto-start mode, don't let them stop, because it'd
			// just auto-start again
			if (isExternalClockActive()) {
				if (usingAnalogClockInput && analogClockInputAutoStart) {
					return;
				}
			}
			else if (!isEitherClockActive()) {
				finishTempolessRecording(false, buttonPressLatency);
				return;
			}

			endPlayback();
		}
	}
}

static const uint32_t recordButtonUIModes[] = {UI_MODE_HORIZONTAL_ZOOM, UI_MODE_HORIZONTAL_SCROLL,
                                               UI_MODE_RECORD_COUNT_IN, UI_MODE_AUDITIONING, 0};

void PlaybackHandler::recordButtonPressed() {

	if (isUIModeWithinRange(recordButtonUIModes)) {

		if (recording == RecordingMode::OFF) {
			actionLogger.closeAction(ActionType::RECORD);
		}

		// Disallow recording to begin if song pre-loaded
		if (recording == RecordingMode::OFF && preLoadedSong) {
			return;
		}

		bool wasRecordingArrangement = (recording == RecordingMode::ARRANGEMENT);

		if (recording == RecordingMode::OFF) {
			recording = RecordingMode::NORMAL;
		}
		else {
			recording = RecordingMode::OFF;
		}

		if (recording == RecordingMode::OFF) {

			if (!wasRecordingArrangement && playbackState) {
				if (currentPlaybackMode == &session) {
					bool anyClipsRemoved = currentSong->deletePendingOverdubs(NULL, NULL, true);
					if (anyClipsRemoved) {
						// use root UI in case this is called from performance view
						sessionView.requestRendering(getRootUI());
					}
				}
				else {
					arrangement.endAnyLinearRecording();
				}
			}
		}
		setLedStates();
		if (wasRecordingArrangement) {
			currentSong->setParamsInAutomationMode(false);
			currentSong->endInstancesOfActiveClips(getActualArrangementRecordPos());
			currentSong->resumeClipsClonedForArrangementRecording();
			view.setModLedStates(); // Set song LED back
		}
	}
}

void PlaybackHandler::setupPlaybackUsingInternalClock(int32_t buttonPressLatency, bool allowCountIn,
                                                      bool restartingPlayback) {
	if (!currentSong) {
		return;
	}

	decideOnCurrentPlaybackMode(); // Must be done up here - we reference currentPlaybackMode a bit below

	int32_t newPos = 0;

	RootUI* rootUI = getRootUI();

	bool isArrangerView = rootUI == &arrangerView
	                      || (rootUI == &performanceSessionView && currentSong->lastClipInstanceEnteredStartPos != -1)
	                      || (rootUI == &automationView && automationView.onArrangerView);

	bool alternativePlaybackStartBehaviour =
	    runtimeFeatureSettings.get(RuntimeFeatureSettingType::AlternativePlaybackStartBehaviour)
	    == RuntimeFeatureStateToggle::On;

	bool accessibility = FlashStorage::accessibilityShortcuts;

	bool startFromCurrentScreenPressed =
	    (accessibility && Buttons::isButtonPressed(deluge::hid::button::CROSS_SCREEN_EDIT))
	    || (!accessibility && Buttons::isButtonPressed(deluge::hid::button::X_ENC));

	bool recordingToArranger = isArrangerView && (recording == RecordingMode::NORMAL);
	bool inArrangerCrossScreen = isArrangerView && currentSong->arrangerAutoScrollModeActive;
	bool isArrangementPadPressed = isArrangerView && isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW);

	int32_t navSys;
	// we might not need this but just always grab it for simplicity
	if (rootUI && rootUI->toTimelineView()) {
		navSys = ((TimelineView*)rootUI)->getNavSysId();
	}
	else {
		navSys = NAVIGATION_CLIP; // Keyboard view will cause this case
	}
	// playback command - restart should always restart, regardless of what else you're doing. play command follows play
	// button logic
	if (restartingPlayback) {
		newPos = 0;
	}
	// second priority - if you're holding an arranger pad then restart from there
	else if (isArrangementPadPressed) {
		newPos = arrangerView.lastInteractedArrangementPos;
	}
	// next is <> + play, or recording into arranger - start from the current left edge scroll position
	// this is good even for cross screen playback since the last cursor position isn't visible
	else if (startFromCurrentScreenPressed || recordingToArranger || inArrangerCrossScreen) {
		newPos = currentSong->xScroll[navSys];
	}

	// See if we're gonna do a tempoless record
	bool doingTempolessRecord = currentPlaybackMode->wantsToDoTempolessRecord(newPos);

	if (!doingTempolessRecord) {
		buttonPressLatency = 0;
	}

	// See if we want a count-in
	if (allowCountIn && !doingTempolessRecord && recording == RecordingMode::NORMAL && countInBars
	    && (!currentUIMode || currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON) && getCurrentUI() == rootUI) {

		ticksLeftInCountIn = currentSong->getBarLength() * countInBars;
		currentVisualCountForCountIn = 0; // Reset it. In a moment it'll display as 4 - 12.
		currentUIMode = UI_MODE_RECORD_COUNT_IN;
	}
	else {
		ticksLeftInCountIn = 0;
	}

	// Remember that the "next" tick (the one we're gonna do right now) is to happen at now-time

	numOutputClocksWaitingToBeSent = 0;

	// Want to send start / continue message, probably
	if (currentlySendingMIDIOutputClocks()) {
		if (newPos) {                                            // "continue"
			sendOutPositionViaMIDI(newPos, !ticksLeftInCountIn); // "Continue" message will only be sent if no count-in
		}
		else { // "start"
			if (!doingTempolessRecord && !ticksLeftInCountIn) {
				midiEngine.sendStart(this);
			}
		}
	}

	int32_t newPlaybackState = PLAYBACK_SWITCHED_ON;
	if (!doingTempolessRecord) {
		newPlaybackState |= PLAYBACK_CLOCK_INTERNAL_ACTIVE;
	}

	// The next timer tick will be 0, and we need to set this here because the call to setupPlayback(), below,
	// may well do things which need to look at the current internal tick count (e.g. AudioClip::resume()), and that
	// reads from this.
	nextTimerTickScheduled = 0;

	setupPlayback(newPlaybackState, newPos, true, true, buttonPressLatency);

	// Set this *after* calling setupPlayback, which will call the audio routine before we do the first tick
	timeNextTimerTickBig = (uint64_t)AudioEngine::audioSampleTimer << 32;

	swungTicksTilNextEvent = 0; // Need to ensure this, here, otherwise, it'll be set to some weird thing by some recent
	                            // call to expectEvent()
}

bool PlaybackHandler::currentlySendingMIDIOutputClocks() {
	return midiOutClockEnabled;
}

uint32_t PlaybackHandler::timerTicksToOutputTicks(uint32_t timerTicks) {
	// If inside world spinning faster than outside world
	if (currentSong->insideWorldTickMagnitude > 0) {
		timerTicks >>= currentSong->insideWorldTickMagnitude;

		// If outside world spinning faster than inside world
	}
	else if (currentSong->insideWorldTickMagnitude < 0) {
		timerTicks <<= (0 - currentSong->insideWorldTickMagnitude);
	}

	return timerTicks;
}

void PlaybackHandler::tapTempoAutoSwitchOff() {
	tapTempoNumPresses = 0;
	setLedStates();
}

void PlaybackHandler::decideOnCurrentPlaybackMode() {
	// If in arranger...
	if (getRootUI() == &arrangerView
	    || (!getRootUI() && currentSong && currentSong->lastClipInstanceEnteredStartPos != -1)) {
		goto useArranger;
	}

	if (!rootUIIsClipMinderScreen() && currentSong->lastClipInstanceEnteredStartPos != -1) {
		goto useArranger;
	}

	if (rootUIIsClipMinderScreen()
	    && (currentSong->lastClipInstanceEnteredStartPos != -1 || getCurrentClip()->isArrangementOnlyClip())) {
useArranger:
		currentPlaybackMode = &arrangement;
	}
	else {
		currentPlaybackMode = &session;
	}
}

// Call decideOnCurrentPlaybackMode() before this
void PlaybackHandler::setupPlayback(int32_t newPlaybackState, int32_t playFromPos, bool doOneLastAudioRoutineCall,
                                    bool shouldShiftAccordingToClipInstance,
                                    int32_t buttonPressLatencyForTempolessRecord) {

	actionLogger.closeAction(ActionType::RECORD);

	if (shouldShiftAccordingToClipInstance && currentPlaybackMode == &arrangement && rootUIIsClipMinderScreen()) {
		playFromPos += currentSong->lastClipInstanceEnteredStartPos;
	}

	ignoringMidiClockInput = false;
	stopOutputRecordingAtLoopEnd = false;

	lastSwungTickActioned = 0;
	lastTriggerClockOutTickDone = -1;
	lastMIDIClockOutTickDone = -1;

	swungTickScheduled = false;
	triggerClockOutTickScheduled = false;
	midiClockOutTickScheduled = false;

	swungTicksTilNextEvent = 0; // Until recently (Aug 2019) I did not do this here for some reason I can't remember and
	                            // insanely most stuff worked

	playbackState = newPlaybackState;
	cvEngine.playbackBegun(); // Call this *after* playbackState is set. If there's a count-in, nothing will happen

	if (getRootUI() && getCurrentUI() == getRootUI()) {
		getRootUI()->notifyPlaybackBegun();
	}

	currentPlaybackMode->setupPlayback(); // This doesn't call the audio routine

	arrangerView.reassessWhetherDoingAutoScroll(playFromPos);

	setLedStates();

	if (doOneLastAudioRoutineCall) {
		// Bit sneaky - we want to call the audio routine one last time, but need it to think that playback's not
		// happening so it doesn't do the first tick yet
		playbackState = 0;
		AudioEngine::routineWithClusterLoading(); // -----------------------------------
		playbackState = newPlaybackState;
	}

	// May not call audio routine during this, cos that'd try doing ticks before everything's set up
	bool oldState = AudioEngine::audioRoutineLocked;
	AudioEngine::audioRoutineLocked = true;
	currentlyActioningSwungTickOrResettingPlayPos = true;
	// Have to do this after calling AudioEngine::routine()
	currentPlaybackMode->resetPlayPos(playFromPos, !ticksLeftInCountIn, buttonPressLatencyForTempolessRecord);
	currentlyActioningSwungTickOrResettingPlayPos = false;
	AudioEngine::audioRoutineLocked = oldState;

	posToNextContinuePlaybackFrom = playFromPos;

	metronomeOffset = playFromPos;

	// We can only set these to -1 after calling the stuff above
	// lastSwungTickDone = -1;

	// when starting playback send updated feedback values for the current clip
	// or active clip selected for midi follow control
	view.sendMidiFollowFeedback();
}

void PlaybackHandler::endPlayback() {
	if (isInternalClockActive() && currentlySendingMIDIOutputClocks()) {
		midiEngine.sendStop(this);
	}

	ignoringMidiClockInput = false;
	if (currentUIMode == UI_MODE_RECORD_COUNT_IN) {
		currentUIMode = UI_MODE_NONE;
	}

	bool wasRecordingArrangement = (recording == RecordingMode::ARRANGEMENT);

	// Rohan: Must happen after currentSong->endInstancesOfActiveClips() is called (ok I can't
	// remember why I wrote that, and now it needs to happen before so that
	// Clip::beingRecordedFrom is still set when playback ends, so notes stop)
	bool shouldDoInstantSongSwap = currentPlaybackMode->endPlayback();

	// Do this after calling currentPlaybackMode->endPlayback(), cos for arrangement that has to get the current
	// tick, which needs to refer to which clock is active, which is stored in playbackState.
	playbackState = 0;

	cvEngine.playbackEnded(); // Call this *after* playbackState is set
	PadLEDs::clearTickSquares();

	// Sometimes, ending playback will trigger an instant song swap
	if (shouldDoInstantSongSwap) {
		doSongSwap(); // Has to happen after setting playbackState = 0, above
	}

	// Or if not doing a song swap, some UI stuff to do
	else {
		// if we're exporting stems, just turn recording mode off
		// we don't want to stop active clip instances because we want to let tails ring out
		if (stemExport.processStarted) {
			recording = RecordingMode::OFF;
			view.setModLedStates();
		}
		else if (wasRecordingArrangement) {
			currentSong->endInstancesOfActiveClips(getActualArrangementRecordPos(), true);
			recording = RecordingMode::OFF;
			view.setModLedStates();
		}

		if (currentSong && getRootUI()) {
			getRootUI()->playbackEnded();
		}
	}

	if (currentVisualCountForCountIn) {
		currentVisualCountForCountIn = 0;
		display->cancelPopup(); // In case the count-in was showing
	}

	setLedStates();
}

void PlaybackHandler::getMIDIClockOutTicksToInternalTicksRatio(uint32_t* internalTicksPer,
                                                               uint32_t* midiClockOutTicksPer) {
	*internalTicksPer = 1; // 12 rather than 24 so we get twice as many ticks, because only every 2nd one is rising-edge
	*midiClockOutTicksPer = 1;

	int16_t magnitudeAdjustment = currentSong->insideWorldTickMagnitude;

	if (magnitudeAdjustment >= 0) {
		*internalTicksPer <<= magnitudeAdjustment;
	}
	else {
		*midiClockOutTicksPer <<= -magnitudeAdjustment;
	}
}

uint32_t PlaybackHandler::getTimePerInternalTick() {
	if (isExternalClockActive()) {
		return timePerInternalTickMovingAverage; // lowpassedTimePerInternalTick
	}
	else {
		return currentSong->getTimePerTimerTickRounded();
	}
}

uint64_t PlaybackHandler::getTimePerInternalTickBig() {
	if (isExternalClockActive()) {
		return (uint64_t)timePerInternalTickMovingAverage << 32;
	}
	else {
		return currentSong->timePerTimerTickBig;
	}
}

float PlaybackHandler::getTimePerInternalTickFloat() {
	if (isExternalClockActive()) {
		return timePerInternalTickMovingAverage;
	}
	else {
		return currentSong->getTimePerTimerTickFloat();
	}
}

void PlaybackHandler::scheduleNextTimerTick(uint32_t doubleSwingInterval) {

	nextTimerTickScheduled = lastTimerTickActioned + doubleSwingInterval;

	uint64_t timeTilNextBig =
	    currentSong->timePerTimerTickBig
	    * (uint64_t)doubleSwingInterval; // To improve: this can overflow if tempo down around 0.001 BPM
	timeNextTimerTickBig = timeLastTimerTickBig + timeTilNextBig;
}

bool currentlyActioningTimerTick = false;

void PlaybackHandler::actionTimerTick() {

	currentlyActioningTimerTick = true;

	// Do this timer tick
	lastTimerTickActioned = nextTimerTickScheduled;
	timeLastTimerTickBig = timeNextTimerTickBig;

	// Do any swung ticks up to and including now.
	// Warning - this could do a song swap and reset a bunch of stuff
	while (lastSwungTickActioned + swungTicksTilNextEvent < lastTimerTickActioned) {
		actionSwungTick();
	}
	// avoid infinite loop when swung ticks til next event is 0 (e.g. something wants to be rechecked next render)
	if (lastSwungTickActioned + swungTicksTilNextEvent == lastTimerTickActioned) {
		actionSwungTick();
	}

	actionTimerTickPart2();

	currentlyActioningTimerTick = false;
}

// 2nd part of timer tick action. We need to be able to call just this part if a song swap has occurred *not* at the
// same time as a timer tick
void PlaybackHandler::actionTimerTickPart2() {

	// Schedule next timer tick. Normal case is two swing intervals away.
	int32_t leftShift = 10 - currentSong->swingInterval;
	leftShift = std::max(leftShift, 0_i32);
	uint32_t timeTilNextTimerTick = 3 << (leftShift); // That's doubleSwingInterval

	// But if count-in happening, limit it to one bar's length (or perhaps this should be one beat, to avoid slight
	// screwiness with very long swing intervals?)
	if (ticksLeftInCountIn) {
		uint32_t limit = currentSong->getBarLength();
		if (timeTilNextTimerTick > limit) {
			timeTilNextTimerTick = limit;
		}
	}

	scheduleNextTimerTick(timeTilNextTimerTick);

	// Schedule another swung tick
	scheduleSwungTickFromInternalClock();

	// If not in count-in...
	if (!ticksLeftInCountIn) {

		currentSong->resyncLFOs();
		if (!stemExport.processStarted || (stemExport.processStarted && !stemExport.renderOffline)) {
			// Trigger clock output ticks
			if (cvEngine.isTriggerClockOutputEnabled()) {
				// Do any trigger clock output ticks up to and including now
				uint32_t internalTicksPer;
				uint32_t analogOutTicksPer;
				getAnalogOutTicksToInternalTicksRatio(&internalTicksPer, &analogOutTicksPer);
				uint64_t fractionLastTimerTick = lastTimerTickActioned * analogOutTicksPer;
				uint64_t fractionNextAnalogOutTick = (lastTriggerClockOutTickDone + 1) * internalTicksPer;

				// if we've fallen behind the timer ticks somehow then scheduling won't work properly so output
				// immediately otherwise set it to be done by the audio engine
				if (fractionNextAnalogOutTick < fractionLastTimerTick) {
					doTriggerClockOutTick();
					fractionNextAnalogOutTick += internalTicksPer;
				}
				// Schedule another trigger clock output tick
				scheduleTriggerClockOutTickParamsKnown(analogOutTicksPer, fractionLastTimerTick,
				                                       fractionNextAnalogOutTick);
			}

			// MIDI clock output ticks
			if (currentlySendingMIDIOutputClocks()) {
				// Do any MIDI clock output ticks up to and including now
				uint32_t internalTicksPer;
				uint32_t midiClockOutTicksPer;
				getMIDIClockOutTicksToInternalTicksRatio(&internalTicksPer, &midiClockOutTicksPer);
				uint64_t fractionLastTimerTick = lastTimerTickActioned * midiClockOutTicksPer;

				uint64_t fractionNextMIDIClockOutTick = (lastMIDIClockOutTickDone + 1) * internalTicksPer;
				// if we've fallen behind the timer ticks somehow then scheduling won't work properly so output
				// immediately otherwise set it to be done by the audio engine
				if (fractionNextMIDIClockOutTick < fractionLastTimerTick) {
					doMIDIClockOutTick();
					fractionNextMIDIClockOutTick += midiClockOutTicksPer;
				}

				// Schedule another MIDI clock output tick
				scheduleMIDIClockOutTickParamsKnown(midiClockOutTicksPer, fractionLastTimerTick,
				                                    fractionNextMIDIClockOutTick);
			}
		}
	}
}

void PlaybackHandler::doTriggerClockOutTick() {
	triggerClockOutTickScheduled = false;
	lastTriggerClockOutTickDone++;
	if (skipAnalogClocks) {
		skipAnalogClocks--;
	}
	else {
		cvEngine.analogOutTick();
	}
}

// Check these are enabled before calling this!
void PlaybackHandler::scheduleTriggerClockOutTick() {

	uint32_t internalTicksPer;
	uint32_t analogOutTicksPer;
	getAnalogOutTicksToInternalTicksRatio(&internalTicksPer, &analogOutTicksPer);
	uint64_t fractionLastTimerTick = lastTimerTickActioned * analogOutTicksPer;
	uint64_t fractionNextAnalogOutTick = (lastTriggerClockOutTickDone + 1) * internalTicksPer;

	scheduleTriggerClockOutTickParamsKnown(analogOutTicksPer, fractionLastTimerTick, fractionNextAnalogOutTick);
}

void PlaybackHandler::scheduleTriggerClockOutTickParamsKnown(uint32_t analogOutTicksPer, uint64_t fractionLastTimerTick,
                                                             uint64_t fractionNextAnalogOutTick) {
	if (fractionNextAnalogOutTick < nextTimerTickScheduled * analogOutTicksPer) {
		triggerClockOutTickScheduled = true;
		timeNextTriggerClockOutTick =
		    (timeLastTimerTickBig
		     + ((uint64_t)((fractionNextAnalogOutTick - fractionLastTimerTick) * currentSong->timePerTimerTickBig))
		           / analogOutTicksPer)
		    >> 32;
	}
}

// Check these are enabled before calling this!
void PlaybackHandler::scheduleMIDIClockOutTick() {

	uint32_t internalTicksPer;
	uint32_t midiClockOutTicksPer;
	getMIDIClockOutTicksToInternalTicksRatio(&internalTicksPer, &midiClockOutTicksPer);
	uint64_t fractionLastTimerTick = lastTimerTickActioned * midiClockOutTicksPer;
	uint64_t fractionNextMIDIClockOutTick = (lastMIDIClockOutTickDone + 1) * internalTicksPer;

	scheduleMIDIClockOutTickParamsKnown(midiClockOutTicksPer, fractionLastTimerTick, fractionNextMIDIClockOutTick);
}

void PlaybackHandler::scheduleMIDIClockOutTickParamsKnown(uint32_t midiClockOutTicksPer, uint64_t fractionLastTimerTick,
                                                          uint64_t fractionNextMIDIClockOutTick) {
	if (fractionNextMIDIClockOutTick < nextTimerTickScheduled * midiClockOutTicksPer) {
		midiClockOutTickScheduled = true;
		timeNextMIDIClockOutTick =
		    (timeLastTimerTickBig
		     + ((uint64_t)((fractionNextMIDIClockOutTick - fractionLastTimerTick) * currentSong->timePerTimerTickBig))
		           / midiClockOutTicksPer)
		    >> 32;
	}
}

void PlaybackHandler::doMIDIClockOutTick() {
	// we need to flush the buffer in case there's a clock in it, otherwise both will be sent at once
	if (midiEngine.anythingInOutputBuffer()) {
		midiEngine.flushMIDI();
	}
	midiClockOutTickScheduled = false;
	lastMIDIClockOutTickDone++;
	if (skipMidiClocks) {
		skipMidiClocks--;
	}
	else {
		midiEngine.sendClock(this, true);
	}
}

void PlaybackHandler::actionSwungTick() {

	currentlyActioningSwungTickOrResettingPlayPos = true;

	swungTickScheduled = false;

	lastSwungTickActioned += swungTicksTilNextEvent;

	int32_t swungTickIncrement; // Set it up up here so a goto, below, works

	bool swappedSong;

	// If count-in still happening...
	if (ticksLeftInCountIn) {

		ticksLeftInCountIn -= swungTicksTilNextEvent;

		// If count-in finished right now...
		if (!ticksLeftInCountIn) {

			// Very crudely reset a bunch of stuff to 0
			nextTimerTickScheduled -= lastTimerTickActioned;
			lastTimerTickActioned = 0;
			lastSwungTickActioned = 0;
			swungTicksTilNextEvent = 0;

			// May not call audio routine during this, cos that'd try doing ticks before everything's set up
			currentPlaybackMode->resetPlayPos(
			    posToNextContinuePlaybackFrom); // Have to do this after calling AudioEngine::routine()

			// Perhaps we'd like to send a MIDI "start" message, cos we didn't before
			cvEngine.playbackBegun();

			if (currentlySendingMIDIOutputClocks()) {
				if (posToNextContinuePlaybackFrom) {
					midiEngine.sendContinue(this); // Position pointer was already sent when "play" pressed
				}
				else {
					midiEngine.sendStart(this);
				}
			}

			display->cancelPopup();
			currentVisualCountForCountIn = 0;
			currentUIMode &= UI_MODE_AUDITIONING;

			// And then yeah, just keep going below
		}

		// Or if there was still more count-in left...
		else {

			int32_t newVisualCount = (uint32_t)(ticksLeftInCountIn - 1) / currentSong->getQuarterNoteLength() + 1;

			if (newVisualCount != currentVisualCountForCountIn) {
				currentVisualCountForCountIn = newVisualCount;
				char buffer[12];
				intToString(newVisualCount, buffer);
				display->displayPopup(buffer, 0, true, 255, 2);
			}
			swungTicksTilNextEvent = 2147483647;
			goto doMetronome;
		}
	}

	// This may possibly swap song, and / or even change currentPlaybackMode.
	// The latter is why we do still have to have this as a separate function.
	swappedSong = currentPlaybackMode->considerLaunchEvent(swungTicksTilNextEvent);

	swungTickIncrement = swungTicksTilNextEvent; // This might have got reset to 0 if there was a song swap
	swungTicksTilNextEvent = 2147483647;

	if (isEitherClockActive()) { // Occasionally, considerLaunchEvent() will stop playback
		currentPlaybackMode->doTickForward(swungTickIncrement);

		if (isEitherClockActive()) { // Occasionally, doTickForward() will stop playback

			// If we swapped song on just a swung tick that wasn't concurrent with a timer tick (unusual), we need to go
			// do some stuff that would normally happen as part of the timer tick
			if (swappedSong && isInternalClockActive() && !currentlyActioningTimerTick) {
				actionTimerTickPart2();
			}

			// If metronome on, make sure we stop on the right tick for that...
			if (metronomeOn) {
doMetronome:
				uint64_t currentMetronomeTick = lastSwungTickActioned;
				if (!ticksLeftInCountIn) {
					currentMetronomeTick += metronomeOffset;
				}

				uint32_t swungTicksPerQuarterNote = currentSong->getQuarterNoteLength();

				if ((currentMetronomeTick % swungTicksPerQuarterNote) == 0) {
					uint32_t phaseIncrement =
					    ((currentMetronomeTick % (swungTicksPerQuarterNote << 2)) == 0) ? 128411753 : 50960238;
					AudioEngine::metronome.trigger(phaseIncrement);
				}

				int32_t ticksIntoCurrentBeep = currentMetronomeTick % swungTicksPerQuarterNote;
				int32_t swungTicksTilNextMetronomeEvent =
				    swungTicksPerQuarterNote - ticksIntoCurrentBeep; // Ticks til next beep
				swungTicksTilNextEvent = std::min(swungTicksTilNextEvent, swungTicksTilNextMetronomeEvent);
			}
		}
	}

	currentlyActioningSwungTickOrResettingPlayPos = false;
}

void PlaybackHandler::scheduleSwungTick() {
	if (isInternalClockActive()) {
		scheduleSwungTickFromInternalClock();
	}
	else {
		scheduleSwungTickFromExternalClock();
	}
}

// This may be called when there already is one scheduled, to reschedule it, if the tempo has changed
void PlaybackHandler::scheduleSwungTickFromInternalClock() {

	if (swungTicksTilNextEvent < 1) {
		swungTicksTilNextEvent = 1; // Shouldn't ultimately be necessary, but...
	}

	int64_t nextSwungTick = lastSwungTickActioned + swungTicksTilNextEvent;

	if (nextSwungTick < nextTimerTickScheduled) {
		swungTickScheduled = true;
		uint32_t swungTicksIntoTimerBit = nextSwungTick - lastTimerTickActioned;

		if (currentSong->hasAnySwing()) {
			int32_t leftShift = 9 - currentSong->swingInterval;
			leftShift = std::max(leftShift, 0_i32);
			uint32_t swingInterval = 3 << (leftShift);

			// Before swing mid-point
			if (swungTicksIntoTimerBit <= swingInterval) {
				uint64_t timeInBig = currentSong->timePerTimerTickBig * swungTicksIntoTimerBit
				                     * (uint32_t)(50 + currentSong->swingAmount) / 50;
				scheduledSwungTickTime = (uint64_t)(timeLastTimerTickBig + timeInBig) >> 32;
			}

			// After swing mid-point
			else {
				uint32_t swungTicksTilEnd = (swingInterval << 1) - swungTicksIntoTimerBit;
				uint64_t timeTilEndBig = currentSong->timePerTimerTickBig * swungTicksTilEnd
				                         * (uint32_t)(50 - currentSong->swingAmount) / 50;
				scheduledSwungTickTime = (uint64_t)(timeNextTimerTickBig - timeTilEndBig) >> 32;
			}
		}
		else {
			scheduledSwungTickTime =
			    (timeLastTimerTickBig + swungTicksIntoTimerBit * currentSong->timePerTimerTickBig) >> 32;
		}
	}
}

// This just uses the rounded timePerTimerTick. Should be adequate
int32_t PlaybackHandler::getNumSwungTicksInSinceLastTimerTick(uint32_t* timeRemainder) {

	// If first timer tick not actioned yet (currently the only function that calls this function already separately
	// deals with this though)...
	if (!nextTimerTickScheduled) {
		if (timeRemainder) {
			*timeRemainder = 0;
		}
		return 0;
	}

	uint32_t timePerTimerTick = currentSong->getTimePerTimerTickRounded();

	uint32_t timePassed = AudioEngine::audioSampleTimer - (uint32_t)(timeLastTimerTickBig >> 32);

	if (currentSong->hasAnySwing()) {

		if (timeRemainder) {
			*timeRemainder = 0; // To be improved
		}

		int32_t leftShift = 9 - currentSong->swingInterval;
		leftShift = std::max(leftShift, 0_i32);
		uint32_t swingInterval = 3 << (leftShift);

		// First, see if we're in the first half still
		uint32_t timePassedFiddledWith = (uint32_t)(timePassed * 50) / (uint32_t)(50 + currentSong->swingAmount);
		uint32_t ticksIn = timePassedFiddledWith / timePerTimerTick;
		if (ticksIn < swingInterval) {
			return ticksIn;
		}

		// Or if we're still here, it's in the second half
		uint32_t timeTilNextTimerTick = (uint32_t)(timeNextTimerTickBig >> 32) - AudioEngine::audioSampleTimer;
		uint32_t timeTilNextFiddledWith =
		    (uint32_t)(timeTilNextTimerTick * 50) / (uint32_t)(50 - currentSong->swingAmount);
		if (!timeTilNextFiddledWith) {
			return 1; // Otherwise we'd get a negative number when subtracting 1 below
		}
		int32_t ticksTilEnd = (uint32_t)(timeTilNextFiddledWith - 1) / (uint32_t)timePerTimerTick + 1; // Rounds up.
		return (swingInterval << 1) - ticksTilEnd;
	}

	else {
		int32_t numSwungTicks = timePassed / timePerTimerTick;
		if (timeRemainder) {
			*timeRemainder = timePassed - numSwungTicks * timePerTimerTick;
		}
		return numSwungTicks;
	}
}

int32_t PlaybackHandler::getNumSwungTicksInSinceLastActionedSwungTick(uint32_t* timeRemainder) {
	if (currentlyActioningSwungTickOrResettingPlayPos) {
		if (timeRemainder) {
			*timeRemainder = 0;
		}
		return 0; // This will save some time, even though it was already returning the correct result.
	}

	return getActualSwungTickCount(timeRemainder) - lastSwungTickActioned;
}

int64_t PlaybackHandler::getActualSwungTickCount(uint32_t* timeRemainder) {

	int64_t actualSwungTick;

	// Internal clock
	if (isInternalClockActive()) {

		// If first timer tick not actioned yet (not sure if we ever get called in this case, though)...
		if (!nextTimerTickScheduled) {
			if (timeRemainder) {
				*timeRemainder = 0;
			}
			return 0;
		}

		actualSwungTick = lastTimerTickActioned + getNumSwungTicksInSinceLastTimerTick(timeRemainder);
	}

	// External clock
	else {

		if (timeRemainder) {
			*timeRemainder = 0; // TODO: fix this!
		}

		float currentInternalTick = getCurrentInternalTickFloatFollowingExternalClock();

		// No swing
		if (!currentSong->hasAnySwing()) {
			actualSwungTick = currentInternalTick;
		}

		// Yes swing
		else {

			int32_t leftShift = 9 - currentSong->swingInterval;
			leftShift = std::max(leftShift, 0_i32);
			uint32_t swingInterval = 3 << (leftShift);
			uint32_t doubleSwingInterval = swingInterval << 1;

			uint64_t startOfSwingBlock = (uint64_t)currentInternalTick / doubleSwingInterval * doubleSwingInterval;
			float posWithinSwingBlock = currentInternalTick - startOfSwingBlock;

			// First, see if we're in the first half still
			float swungTicksIn = (uint32_t)(posWithinSwingBlock * 50) / (uint32_t)(50 + currentSong->swingAmount);
			if (swungTicksIn < swingInterval) {
				actualSwungTick = startOfSwingBlock + swungTicksIn;
			}

			// Or, if we're in the second half
			else {
				float posTilEndOfSwingBlock = (float)doubleSwingInterval - posWithinSwingBlock;
				float swungTicksTilEnd =
				    (uint32_t)(posTilEndOfSwingBlock * 50) / (uint32_t)(50 - currentSong->swingAmount);
				actualSwungTick = startOfSwingBlock + doubleSwingInterval - (swungTicksTilEnd + 1); // Round that bit up
			}
		}
	}

	// Make sure the result isn't outside of its possible range

	if (actualSwungTick < lastSwungTickActioned) {
		actualSwungTick = lastSwungTickActioned;
		if (timeRemainder) {
			*timeRemainder = 0;
		}
	}

	else {
		int64_t nextSwungTickToAction = lastSwungTickActioned + swungTicksTilNextEvent;
		if (nextSwungTickToAction // Checking for special case when nextSwungTickToAction == 0, when playback first
		                          // starts. Unchecked, that would sometimes lead to us returning -1 when following
		                          // external clock.
		    && actualSwungTick >= nextSwungTickToAction) {
			actualSwungTick = nextSwungTickToAction - 1;
			if (timeRemainder) {
				*timeRemainder = getTimePerInternalTick() - 1; // A bit cheesy...
			}
		}
	}

	return actualSwungTick;
}

int64_t PlaybackHandler::getCurrentInternalTickCount(uint32_t* timeRemainder) {

	uint32_t timePerTimerTick = currentSong->getTimePerTimerTickRounded();

	int64_t internalTickCount;

	// Internal clock
	if (isInternalClockActive()) {

		// If no timer ticks have occurred yet, the answer is a resounding zero, and we have to have this as a special
		// case because timeLastTimerTickBig won't have been set yet. This will happen all the time during playback
		// setup, e.g. in AudioClip::resumePlayback()
		if (!nextTimerTickScheduled) {
			if (timeRemainder) {
				*timeRemainder = 0;
			}
			internalTickCount = 0;
		}

		// Or, the normal case - calculate the answer
		else {
			uint32_t timeSinceLastTimerTick = AudioEngine::audioSampleTimer - (uint32_t)(timeLastTimerTickBig >> 32);
			int32_t ticksSinceLastTimerTick = timeSinceLastTimerTick / timePerTimerTick;
			if (timeRemainder) {
				*timeRemainder = timeSinceLastTimerTick - (ticksSinceLastTimerTick * timePerTimerTick);
			}
			internalTickCount = lastTimerTickActioned + ticksSinceLastTimerTick;

			// Safety against rounding errors - make sure we don't give an internal tick count that's less than a swung
			// tick we already actioned. I've seen this happen (or at least become apparent) when at 6144 resolution
			// with very shortened audio clip.
			if (internalTickCount < lastSwungTickActioned) {
				internalTickCount = lastSwungTickActioned;
			}
		}
	}

	// External clock
	else {

		if (timeRemainder) {
			*timeRemainder = 0; // TODO: fix this!
		}

		internalTickCount = getCurrentInternalTickFloatFollowingExternalClock();
	}

#if ALPHA_OR_BETA_VERSION
	if (internalTickCount < 0) {
		// Trying to narrow down "nofg" error, which Ron got most recently (Nov 2021). Wait no, he didn't have playback
		// on!
		FREEZE_WITH_ERROR("E429");
	}
#endif

	return internalTickCount;
}

float PlaybackHandler::getCurrentInternalTickFloatFollowingExternalClock() {

	// If we've only actually received one (or none - is that possible?) input tick...
	if (lastInputTickReceived <= 0) {
		// We're before it, which won't make sense to the caller, so just say we're at 0
		return 0;
	}

	int32_t t = 0;

	int32_t timeSinceLastInputTick = AudioEngine::audioSampleTimer - timeLastInputTicks[t];

	float currentInputTick;

	// If that input tick hasn't "happened" yet and is currently just scheduled to happen soon, then the current
	// internal tick is before it
	if (timeSinceLastInputTick < 0) {

goAgain:
		int32_t timeSincePreviousInputTick = AudioEngine::audioSampleTimer - timeLastInputTicks[t + 1];

		// If the previous one also hasn't happened yet, look a further one back
		if (timeSincePreviousInputTick < 0) {
			timeSinceLastInputTick = timeSincePreviousInputTick;
			t++;
			if (t >= lastInputTickReceived) {
				return 0; // If all the input ticks received so far have not yet "happened"
			}
			// If we just didn't remember that far back - should never really happen
			if (t >= kNumInputTicksForMovingAverage - 1) {
				// Gonna be inexact, sorry!
				currentInputTick = lastInputTickReceived - kNumInputTicksForMovingAverage;
				goto gotCurrentInputTick;
			}
			goto goAgain;
		}

		// Just see how far apart the last two received input ticks were (even though we haven't actually actioned the
		// most recent one yet)
		int32_t timeBetweenInputTicks = timeLastInputTicks[t] - timeLastInputTicks[t + 1];

		// Should now be impossible for them to be at the same time, since we should be looking at one in the future and
		// one not
		if (ALPHA_OR_BETA_VERSION && timeBetweenInputTicks <= 0) {
			FREEZE_WITH_ERROR("E337");
		}

		currentInputTick =
		    (float)timeSincePreviousInputTick / (uint32_t)timeBetweenInputTicks + lastInputTickReceived - t - 1;
	}

	// Or if it has happened...
	else {
		if (timeSinceLastInputTick >= timePerInputTickMovingAverage) {
			timeSinceLastInputTick = timePerInputTickMovingAverage - 1;
		}
		currentInputTick = (float)timeSinceLastInputTick / timePerInputTickMovingAverage + lastInputTickReceived;
	}

gotCurrentInputTick:
	uint32_t internalTicksPer;
	uint32_t inputTicksPer;
	getInternalTicksToInputTicksRatio(&inputTicksPer, &internalTicksPer);

	float currentInternalTick = currentInputTick / inputTicksPer * internalTicksPer;

	return currentInternalTick;
}

int32_t PlaybackHandler::getInternalTickTime(int64_t internalTickCount) {

	// Internal clock
	if (isInternalClockActive()) {

		// If first timer tick hasn't even occurred yet, various values will not yet be valid. The time of the first
		// tick will not even have been decided. So, use audioDriver.audioSampleTimer in its place, since that is what
		// our returned value will be compared to.
		if (!nextTimerTickScheduled) {
			return AudioEngine::audioSampleTimer
			       + (((int64_t)currentSong->timePerTimerTickBig * internalTickCount) >> 32);
		}

		int32_t numTicksAfterLastTimerTick = internalTickCount - lastTimerTickActioned; // Could be negative
		return (int64_t)(timeLastTimerTickBig + (int64_t)currentSong->timePerTimerTickBig * numTicksAfterLastTimerTick)
		       >> 32;
	}

	// External clock
	else {
		uint32_t internalTicksPer;
		uint32_t inputTicksPer;
		getInternalTicksToInputTicksRatio(&inputTicksPer, &internalTicksPer);

		float inputTickCount = (float)(internalTickCount * inputTicksPer) / internalTicksPer;

		return (int32_t)timeLastInputTicks[0]
		       + (int32_t)((inputTickCount - lastInputTickReceived) * (int32_t)timePerInputTickMovingAverage);
	}
}

// Caller must remove OLED working animation.
void PlaybackHandler::doSongSwap(bool preservePlayPosition) {
	AudioEngine::logAction("PlaybackHandler::doSongSwap start");

	if (currentSong) {

		currentSong->stopAllAuditioning();

		// If playing...
		if (isEitherClockActive()) {

			// If we're preserving the tempo (either cos we chose to or because we're following external clock)
			if (isExternalClockActive() || songSwapShouldPreserveTempo) {

				// Since we're currentlyPlaying, we know that there still is a currentSong

				// We want to keep the old song's tempo. We need to account for the fact that the two songs might have
				// different magnitudes though, before we do anything like copy the timePerTimerTick. So we apply any
				// difference in magnitude to the timePerTimerTick before we copy / compare it.
				int32_t magnitudeDifference =
				    preLoadedSong->insideWorldTickMagnitude - currentSong->insideWorldTickMagnitude;

				if (magnitudeDifference >= 0) {
					currentSong->timePerTimerTickBig >>= magnitudeDifference;
				}
				else {
					currentSong->timePerTimerTickBig <<= (0 - magnitudeDifference);
				}

				// If tempo magnitude matching, we now need to look at how similar the (potentially modified)
				// timePerTimerTick's are, and shift magnitudes if they're really different
				if (tempoMagnitudeMatchingEnabled) {
					while (preLoadedSong->timePerTimerTickBig > currentSong->timePerTimerTickBig * 1.414) {
						currentSong->timePerTimerTickBig <<= 1;
						preLoadedSong->insideWorldTickMagnitude--;
					}
					while (preLoadedSong->timePerTimerTickBig < currentSong->timePerTimerTickBig * 0.707) {
						currentSong->timePerTimerTickBig >>= 1;
						preLoadedSong->insideWorldTickMagnitude++;
					}
				}

				preLoadedSong->timePerTimerTickBig = currentSong->timePerTimerTickBig;
			}

			// Any MIDI or gate notes wouldn't be stopped by our unassignAllVoices() call below
			currentSong->stopAllMIDIAndGateNotesPlaying();
		}
	}

	// Swap stuff over
	AudioEngine::unassignAllVoices(true);
	midiFollow.clearStoredClips(); // need to clear clip pointers stored for previous song
	currentSong = preLoadedSong;
	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	preLoadedSong = NULL;
	loadSongUI.deletedPartsOfOldSong = false;

	currentSong->sendAllMIDIPGMs();
	AudioEngine::getReverbParamsFromSong(currentSong);

	// Some more "if we're playing" stuff - this needs to happen after currentSong is swapped over, because
	// resyncInternalTicksToInputTicks() references it
	if (isEitherClockActive()) {

		// If beginning in arranger, not allowed to preserve play position (the caller never actually tries to do this
		// anyway)
		if (currentSong->lastClipInstanceEnteredStartPos != -1) {
			preservePlayPosition = false;
		}

		// If we are (still) preserving position, do that
		if (preservePlayPosition) {
			resyncInternalTicksToInputTicks(currentSong);
		}

		// Otherwise, reset play position
		else {
			lastTimerTickActioned = 0;
			lastInputTickReceived = 0;
			lastSwungTickActioned = 0;

			lastTriggerClockOutTickDone = -1;
			lastMIDIClockOutTickDone = -1;

			// Set it so the tick which we're gonna do right now, at the start of the new song, has 0 increment
			swungTicksTilNextEvent = 0;
		}

		// And now, if switching to arranger (in which case, remember, we definitely didn't preserve play position), do
		// that
		if (currentSong->lastClipInstanceEnteredStartPos != -1) {
			currentPlaybackMode = &arrangement;
			arrangement.setupPlayback();
			arrangement.resetPlayPos(currentSong->lastClipInstanceEnteredStartPos);
		}

		// Or if we weren't switching to the arranger, the equivalent of that would get called from
		// Session::considerLaunchEvent()
	}

	AudioEngine::bypassCulling = true;

	display->displayLoadingAnimationText("Loading"); // More loading might need to happen now, or still be happening
	currentUIMode = UI_MODE_LOADING_SONG_NEW_SONG_PLAYING;
	AudioEngine::logAction("PlaybackHandler::doSongSwap end");
}

void PlaybackHandler::analogClockRisingEdge(uint32_t time) {

	// If not already playing, start now, if set to auto-start. But not if the settings haven't yet been read
	if (!playbackState) {
		if (analogClockInputAutoStart && FlashStorage::settingsBeenRead) {
			usingAnalogClockInput = true;
			setupPlaybackUsingExternalClock(false);
		}
	}

	if (playbackState) {
		inputTick(true, time);
	}

	timeLastAnalogClockInputRisingEdge = AudioEngine::audioSampleTimer;
}

void PlaybackHandler::setupPlaybackUsingExternalClock(bool switchingFromInternalClock, bool fromContinueCommand) {
	if (!currentSong) {
		return;
	}

	ticksLeftInCountIn = 0;

	numInputTicksToSkip = 0;

	if (switchingFromInternalClock) {

		uint32_t internalTicksPer;
		uint32_t inputTicksPer;
		getInternalTicksToInputTicksRatio(&inputTicksPer, &internalTicksPer);

		lastInputTickReceived = round((uint64_t)getCurrentInternalTickCount() * inputTicksPer / internalTicksPer) - 1;

		playbackState &= ~PLAYBACK_CLOCK_INTERNAL_ACTIVE;
	}
	else {
		lastInputTickReceived = -1;
	}

	// internalTickScheduled = false;

	tempoMagnitudeMatchingActiveNow = (!switchingFromInternalClock && !fromContinueCommand
	                                   && tempoMagnitudeMatchingEnabled && !usingAnalogClockInput);
	// Tempo magnitude matching just wouldn't really make sense if playing from a "continue" command, because each time
	// we realise the tempo is actually double or half, that'd mean we'd have to re-interpret the start-position at the
	// new magnitude, so completely jump the play-cursor to a different place, whose first beat we already missed
	// hearing. Also, Logic, and for that matter the Deluge itself, when outputting a "continue", will output several
	// "clock" messages simultaneously to achieve a finer resolution start position, and when hearing these, well, this
	// looks like infinite tempo.

	numInputTickTimesCounted = 0;

	stickyCurrentTimePerInternalTickInverse = veryCurrentTimePerInternalTickInverse =
	    currentSong->divideByTimePerTimerTick; // Sets defaults
	timePerInternalTickMovingAverage = lowpassedTimePerInternalTick = stickyTimePerInternalTick =
	    currentSong->getTimePerTimerTickRounded();

	slowpassedTimePerInternalTick = stickyTimePerInternalTick << slowpassedTimePerInternalTickSlowness;

	// Imagine the user had just hit the play button - how fast would we expect to see internal ticks happening?
	uint64_t targetedTimePerInternalTick = stickyTimePerInternalTick;

	// Convert this to input tick time - and that's how often we're expecting to see input ticks.
	uint32_t internalTicksPer;
	uint32_t inputTicksPer;
	getInternalTicksToInputTicksRatio(&inputTicksPer, &internalTicksPer);
	targetedTimePerInputTick = targetedTimePerInternalTick * internalTicksPer / inputTicksPer;

	// If we don't yet know the time per input tick, go with the estimation we just made
	if (timePerInputTickMovingAverage == 0) {
		timePerInputTickMovingAverage = targetedTimePerInputTick;
	}

	int32_t newPlaybackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_EXTERNAL_ACTIVE;

	if (!switchingFromInternalClock) {
		bool shouldShiftAccordingToClipInstance = (!fromContinueCommand && posToNextContinuePlaybackFrom == 0);
		decideOnCurrentPlaybackMode();
		setupPlayback(newPlaybackState, posToNextContinuePlaybackFrom, false, shouldShiftAccordingToClipInstance);
		posToNextContinuePlaybackFrom = 0;
	}
	else {
		playbackState = newPlaybackState;
		setLedStates();
	}
}

void PlaybackHandler::positionPointerReceived(uint8_t data1, uint8_t data2) {
	D_PRINTLN("position");
	uint32_t pos = (((uint32_t)data2 << 7) | data1) * 6;

	if (currentSong->insideWorldTickMagnitude >= 0) {
		pos <<= currentSong->insideWorldTickMagnitude;
	}
	else {
		pos >>= (0 - currentSong->insideWorldTickMagnitude);
	}

	// If following external clock right now, jump to the position (actually 1 tick before the position, so the next
	// "clock" message will play that pos)
	if (isExternalClockActive()) {

		// But, to get around a weird "bug" with Ableton and my Mbox, don't do this if pos is 0 and we've only done the
		// first input tick
		if (pos == 0 && lastInputTickReceived == 0) {
			return;
		}

		currentPlaybackMode->resetPlayPos((int32_t)pos);
	}

	// Otherwise, even if playing to internal clock, just store the position, to use if we receive a "continue" message
	else {
		posToNextContinuePlaybackFrom = pos;
	}
}

void PlaybackHandler::startMessageReceived() {
	if (ignoringMidiClockInput || !midiInClockEnabled) {
		return;
	}
	D_PRINTLN("start");
	// If we already are playing
	if (playbackState) {

		// If we received this message only just after having started playback ourself, assume we're getting our output
		// echoed back to us, and just ignore it
		if (startIgnoringMidiClockInputIfNecessary()) {
			return;
		}

		// Otherwise, end our internal playback so we can start playing following an external clock.
		endPlayback();
	}

	usingAnalogClockInput = false;
	posToNextContinuePlaybackFrom = 0; // Start from pos 0
	setupPlaybackUsingExternalClock(false);
}

bool PlaybackHandler::startIgnoringMidiClockInputIfNecessary() {

	if (isInternalClockActive()
	    && (int32_t)(AudioEngine::audioSampleTimer - timeLastMIDIStartOrContinueMessageSent) < 50 * 44) {
		D_PRINTLN("ignoring midi clock input");
		ignoringMidiClockInput = true;
		return true;
	}

	else {
		return false;
	}
}

void PlaybackHandler::continueMessageReceived() {
	if (ignoringMidiClockInput || !midiInClockEnabled) {
		return;
	}

	D_PRINTLN("continue");
	// If we already are playing
	if (playbackState) {

		// If we received this message only just after having started playback ourself, assume we're getting our output
		// echoed back to us, and just ignore it
		if (startIgnoringMidiClockInputIfNecessary()) {
			return;
		}

		// If already following external clock, there's nothing to do: if we already received a song position pointer,
		// that will have already taken effect
		if (isExternalClockActive()) {
			return;
		}

		endPlayback();
	}

	usingAnalogClockInput = false;
	// posToNextContinuePlaybackFrom is left at whatever value it's at - playback will continue from there.
	setupPlaybackUsingExternalClock(false, true);
}

void PlaybackHandler::stopMessageReceived() {
	if (ignoringMidiClockInput || !midiInClockEnabled) {
		return;
	}
	if (isExternalClockActive()) {
		endPlayback();
	}
}

void PlaybackHandler::clockMessageReceived(uint32_t time) {
	if (ignoringMidiClockInput || !midiInClockEnabled) {
		return;
	}
	// D_PRINTLN("clock");

	if (playbackState) {
		inputTick(false, time);
	}
}

void PlaybackHandler::scheduleSwungTickFromExternalClock() {
	uint64_t nextSwungTick = lastSwungTickActioned + swungTicksTilNextEvent;
	uint64_t internalTickPositionForNextSwungTickTimes50;
	if (!currentSong->hasAnySwing()) {
		internalTickPositionForNextSwungTickTimes50 = nextSwungTick * 50;
	}
	else {

		int32_t leftShift = 10 - currentSong->swingInterval;
		leftShift = std::max(leftShift, 0_i32);
		uint32_t doubleSwingInterval = 3 << (leftShift);

		uint32_t swungTickWithinInterval = nextSwungTick % doubleSwingInterval;
		uint64_t startOfSwingInterval = nextSwungTick - swungTickWithinInterval;

		uint32_t internalTickWithinIntervalTimes50;

		// If in first half of interval
		if (swungTickWithinInterval <= (doubleSwingInterval >> 1)) {
			internalTickWithinIntervalTimes50 = swungTickWithinInterval * (50 + currentSong->swingAmount);
		}

		// Or if in second half of interval
		else {
			int32_t ticksTilEnd = doubleSwingInterval - swungTickWithinInterval;
			internalTickWithinIntervalTimes50 =
			    doubleSwingInterval * 50 - ticksTilEnd * (50 - currentSong->swingAmount);
		}

		internalTickPositionForNextSwungTickTimes50 = internalTickWithinIntervalTimes50 + (startOfSwingInterval * 50);
	}

	uint32_t internalTicksPer;
	uint32_t inputTicksPer;
	getInternalTicksToInputTicksRatio(&inputTicksPer, &internalTicksPer);

	int64_t inputTickPositionForNextSwungTickTimes50TimesInternalTicksPer =
	    internalTickPositionForNextSwungTickTimes50 * inputTicksPer;

	int64_t lastInputTickReceivedTimes50 = lastInputTickReceived * 50;

	// If this next swung tick is gonna happen before we're going to receive the *next* input tick
	// (possibly right on this current input tick that we've just received but haven't actually processed yet)...
	if (inputTickPositionForNextSwungTickTimes50TimesInternalTicksPer
	    < (lastInputTickReceivedTimes50 + 50) * internalTicksPer) {
		swungTickScheduled = true;
		uint64_t inputTickFractionTimes50TimesInternalTicksPer =
		    inputTickPositionForNextSwungTickTimes50TimesInternalTicksPer
		    - (lastInputTickReceivedTimes50 * internalTicksPer);
		// TODO: if we're scheduling a swung tick which corresponds to a couple of input ticks back in time, which could
		// happen if they haven't actually "happened" yet and only been "received", I feel like the current method would
		// be quite inexact and we should be looking at timeLastInputTicks[some_higher_number]. Though would that do
		// less smoothing out of jitter?

		// Another important note here: inputTickFractionTimes50TimesInternalTicksPer may be negative because the
		// scheduled swung tick might be further back in time than the scheduled time of the most recently received
		// input tick - I think particularly for very high resolution songs? This is fine, and even if we end up with a
		// time which has already passed, the swung tick will still get actioned on the next audio routine call. The key
		// point is just that the below calculation must correctly deal with negative numbers.
		scheduledSwungTickTime =
		    timeLastInputTicks[0]
		    + (int64_t)(inputTickFractionTimes50TimesInternalTicksPer * timePerInputTickMovingAverage)
		          / (int32_t)(internalTicksPer * 50);
	}
}

void PlaybackHandler::inputTick(bool fromTriggerClock, uint32_t time) {

	if (numInputTicksToSkip > 0) {
		numInputTicksToSkip--;
		return;
	}

	// If we were using the internal clock, we want to start again using the external clock, kinda
	if (isInternalClockActive()) {

		// If we're in our count-in, that's incompatible with following external clock, so we'll just have to ignore the
		// incoming clock for now. This has the slightly weird effect that after the count-in is over, we'll switch to
		// following external clock, so tempo might abruptly change. But that's probably better than just permanently
		// ignoring incoming clock, which could create more unclearness.
		if (ticksLeftInCountIn) {
			return;
		}
		usingAnalogClockInput = fromTriggerClock;
		setupPlaybackUsingExternalClock(true);
	}

	uint32_t timeTilInputTick;

	// The 40 here is a fine-tuned amount to stop everything wrapping wrong when CPU load heavy. 28 to 98 seemed to work
	// correctly
	if (time) {
		timeTilInputTick =
		    (((uint32_t)(time - (uint32_t)AudioEngine::i2sTXBufferPos) >> (2 + NUM_MONO_OUTPUT_CHANNELS_MAGNITUDE))
		     + 40)
		    & (SSI_TX_BUFFER_NUM_SAMPLES - 1);
	}
	else {
		timeTilInputTick = 0;
	}

	uint32_t timeThisInputTick = AudioEngine::audioSampleTimer + timeTilInputTick;

	// If we're doing tempo magnitude matching, do all that
	if (tempoMagnitudeMatchingActiveNow) {
		if (lastInputTickReceived == -1) {
			timeVeryFirstInputTick = timeThisInputTick; // Remember, lastInputTickReceived hasn't been increased yet for
			                                            // the input-tick we're processing right now
		}
		else {
			uint32_t timeSinceVeryFirst = timeThisInputTick - timeVeryFirstInputTick;
			uint32_t expectedTimeSinceVeryFirst =
			    targetedTimePerInputTick
			    * (lastInputTickReceived + 1); // Remember, lastInputTickReceived hasn't been increased yet for the
			                                   // input-tick we're processing right now
			if (expectedTimeSinceVeryFirst < 1) {
				expectedTimeSinceVeryFirst = 1;
			}

			// If input ticks coming in too slow (and not about to overflow any numbers)...
			while (expectedTimeSinceVeryFirst < 2147483648uL
			       && timeSinceVeryFirst > expectedTimeSinceVeryFirst * 1.46) {
				currentSong->insideWorldTickMagnitude++;
				expectedTimeSinceVeryFirst <<= 1;
				targetedTimePerInputTick <<= 1;
			}

			// If input ticks coming in too fast (and not about to make any numbers 0)...
			while (targetedTimePerInputTick > 1 && timeSinceVeryFirst < expectedTimeSinceVeryFirst * 0.68) {
				currentSong->insideWorldTickMagnitude--;
				expectedTimeSinceVeryFirst >>= 1;
				targetedTimePerInputTick >>= 1;
			}

			// If we've done this enough times, don't do it again
			if (lastInputTickReceived >= kNumInputTicksToAllowTempoTargeting) {
				tempoMagnitudeMatchingActiveNow = false;
				D_PRINTLN("finished tempo magnitude matching. magnitude =  %d", currentSong->insideWorldTickMagnitude);
			}
		}
	}

	lastInputTickReceived++;

	// We do need an accurate measure of internal tick time immediately, for syncing LFOs
	if (lastInputTickReceived != 0) {
		uint32_t timeLastInputTickTook = timeThisInputTick - timeLastInputTicks[0];

		D_PRINTLN("time since last:  %d", timeLastInputTickTook);

		uint32_t internalTicksPer;
		uint32_t inputTicksPer;
		getInternalTicksToInputTicksRatio(&inputTicksPer, &internalTicksPer);

		uint32_t thisTimePerInternalTick = timeLastInputTickTook * inputTicksPer / internalTicksPer;

		veryCurrentTimePerInternalTickInverse = 2147483647 / ((thisTimePerInternalTick * 3) >> 1);

		// Lowpass
		int32_t distanceToGo = thisTimePerInternalTick - lowpassedTimePerInternalTick;
		lowpassedTimePerInternalTick += (distanceToGo + (1 << 1)) >> 2;

		// Slowpass
		distanceToGo =
		    thisTimePerInternalTick
		    - (slowpassedTimePerInternalTick >> slowpassedTimePerInternalTickSlowness); // Ok this looks weird...
		slowpassedTimePerInternalTick += distanceToGo;

		// Sticky
		// Or, if new value is just 10% different than lowpassed value, we'll change, too

		// 0.1% = 1074815565
		// 0.2% = 1075889307
		// 0.5% = 1079110533
		// 1% = 1084479242
		// 2% = 1095216660
		// 5% = 1127428915
		if ((lowpassedTimePerInternalTick >> 2) > multiply_32x32_rshift32(1127428915, stickyTimePerInternalTick)
		    || (stickyTimePerInternalTick >> 2) > multiply_32x32_rshift32(1127428915, lowpassedTimePerInternalTick)) {
			// D_PRINTLN("5% tempo jump");
			// D_PRINTLN(lowpassedTimePerInternalTick);
			slowpassedTimePerInternalTick = lowpassedTimePerInternalTick << slowpassedTimePerInternalTickSlowness;
			stickyTimePerInternalTick = lowpassedTimePerInternalTick;

			stickyCurrentTimePerInternalTickInverse = 2147483647 / ((stickyTimePerInternalTick * 3) >> 1);
		}
		else if ((slowpassedTimePerInternalTick >> (slowpassedTimePerInternalTickSlowness + 2))
		             > multiply_32x32_rshift32(1084479242, stickyTimePerInternalTick)
		         || (stickyTimePerInternalTick >> 2) > multiply_32x32_rshift32(
		                1084479242, slowpassedTimePerInternalTick >> slowpassedTimePerInternalTickSlowness)) {
			// D_PRINTLN("1% tempo jump");
			stickyTimePerInternalTick = lowpassedTimePerInternalTick =
			    slowpassedTimePerInternalTick >> slowpassedTimePerInternalTickSlowness;

			stickyCurrentTimePerInternalTickInverse = 2147483647 / ((stickyTimePerInternalTick * 3) >> 1);
		}
	}

	// Calculating time per input tick for the purpose of scheduling internal ticks. TODO: this code largely mirrors
	// what the tempo magnitude matching code above does - could be combined. Or not?
	if (numInputTickTimesCounted) {
		timePerInputTickMovingAverage =
		    (uint32_t)(timeThisInputTick - timeLastInputTicks[numInputTickTimesCounted - 1]) / numInputTickTimesCounted;
		resetTimePerInternalTickMovingAverage();
	}

	if (numInputTickTimesCounted < kNumInputTicksForMovingAverage) {
		numInputTickTimesCounted++;
	}
	for (int32_t i = numInputTickTimesCounted - 1; i >= 1; i--) {
		timeLastInputTicks[i] = timeLastInputTicks[i - 1];
	}

	timeLastInputTicks[0] = timeThisInputTick;

	// Schedule an upcoming swung tick. Can only do this after updating timeLastInputTicks[0], just above.
	// TODO: would it be better, in the case where swungTickScheduled is already true, to go and adjust / re-schedule it
	// now we have more information?
	if (!swungTickScheduled) {
		scheduleSwungTickFromExternalClock();
	}
}

uint32_t PlaybackHandler::getTimePerInternalTickInverse(bool getStickyValue) {
	if (isExternalClockActive()) {
		if (getStickyValue) {
			return stickyCurrentTimePerInternalTickInverse;
		}
		else {
			return veryCurrentTimePerInternalTickInverse;
		}
	}
	else {
		return currentSong->divideByTimePerTimerTick;
	}
}

void PlaybackHandler::resetTimePerInternalTickMovingAverage() {
	// Only do this if no tempo-targeting (that'd be a disaster!!), and if some input ticks have actually been received
	if (!tempoMagnitudeMatchingActiveNow && lastInputTickReceived > 0) {

		uint32_t internalTicksPer;
		uint32_t inputTicksPer;
		getInternalTicksToInputTicksRatio(&inputTicksPer, &internalTicksPer);

		timePerInternalTickMovingAverage = timePerInputTickMovingAverage * inputTicksPer / internalTicksPer;
	}
}

void PlaybackHandler::getAnalogOutTicksToInternalTicksRatio(uint32_t* internalTicksPer, uint32_t* analogOutTicksPer) {
	*internalTicksPer =
	    12; // 12 rather than 24 so we get twice as many ticks, because only every 2nd one is rising-edge
	*analogOutTicksPer = analogOutTicksPPQN;

	int16_t magnitudeAdjustment = currentSong->insideWorldTickMagnitude;

	if (magnitudeAdjustment >= 0) {
		*internalTicksPer <<= magnitudeAdjustment;
	}
	else {
		*analogOutTicksPer <<= -magnitudeAdjustment;
	}
}

void PlaybackHandler::getInternalTicksToInputTicksRatio(uint32_t* inputTicksPer, uint32_t* internalTicksPer) {

	int16_t inputTickMagnitude;
	uint32_t inputTickScale;

	inputTickMagnitude = currentSong->insideWorldTickMagnitude;
	inputTickScale = currentSong->getInputTickScale();

	if (usingAnalogClockInput) {
		*inputTicksPer = analogInTicksPPQN;
		*internalTicksPer = 8; // Will usually get multiplied by 3 ( *= inputTickScale below ) to make 24
	}
	else {
		*inputTicksPer = 3;
		*internalTicksPer = 1; // Will usually get multiplied by 3 ( *= inputTickScale below ) to make 3
	}

	*internalTicksPer *= inputTickScale;

	if (inputTickMagnitude >= 0) {
		*internalTicksPer <<= inputTickMagnitude;
	}
	else {
		*inputTicksPer <<= (0 - inputTickMagnitude);
	}
}

// To be called if we need to skip the analog-out tick count to a different place, because some scaling stuff has
// changed. At this point, whatever device is following our clock will cease to be correctly synced to us
void PlaybackHandler::resyncAnalogOutTicksToInternalTicks() {

	if (!cvEngine.isTriggerClockOutputEnabled()) {
		return;
	}

	uint32_t internalTicksPer;
	uint32_t analogOutTicksPer;
	getAnalogOutTicksToInternalTicksRatio(&internalTicksPer, &analogOutTicksPer);
	lastTriggerClockOutTickDone = (uint64_t)getCurrentInternalTickCount() * analogOutTicksPer / internalTicksPer;
}

void PlaybackHandler::resyncMIDIClockOutTicksToInternalTicks() {

	if (!currentlySendingMIDIOutputClocks()) {
		return;
	}

	uint32_t internalTicksPer;
	uint32_t midiClockOutTicksPer;
	getMIDIClockOutTicksToInternalTicksRatio(&internalTicksPer, &midiClockOutTicksPer);
	lastMIDIClockOutTickDone = (uint64_t)getCurrentInternalTickCount() * midiClockOutTicksPer / internalTicksPer;
}

/** On OLED displayes both Swing amount and interval, on 7seg only the interval. */
void PlaybackHandler::commandDisplaySwingInterval() {
	DEF_STACK_STRING_BUF(text, 30);
	if (display->haveOLED()) {
		text.append("Swing: ");
		if (currentSong->swingAmount == 0) {
			text.append("off");
		}
		else {
			text.appendInt(currentSong->swingAmount + 50);
		}
		text.append("\n");
	}
	syncValueToString(currentSong->swingInterval, text, currentSong->getInputTickMagnitude());
	display->popupTextTemporary(text.c_str(), PopupType::SWING);
}

void PlaybackHandler::commandClearTempoAutomation() {
	currentSong->clearTempoAutomation();
}

/** On OLED displayes both Swing amount and interval, on 7seg only the amount. */
void PlaybackHandler::commandDisplaySwingAmount() {
	DEF_STACK_STRING_BUF(text, 30);
	if (display->haveOLED()) {
		text.append("Swing: ");
		if (currentSong->swingAmount == 0) {
			text.append("off");
		}
		else {
			text.appendInt(currentSong->swingAmount + 50);
		}
		text.append("\n");
		syncValueToString(currentSong->swingInterval, text, currentSong->getInputTickMagnitude());
	}
	else {
		if (currentSong->swingAmount == 0) {
			text.append("OFF");
		}
		else {
			text.appendInt(currentSong->swingAmount + 50);
		}
	}
	display->popupTextTemporary(text.c_str(), PopupType::SWING);
}

void PlaybackHandler::commandEditSwingInterval(int8_t offset) {
	currentSong->changeSwingInterval(wrapSwingIntervalSyncLevel(currentSong->swingInterval + offset));
	commandDisplaySwingInterval();
}

void PlaybackHandler::commandEditSwingAmount(int8_t offset) {
	int32_t newSwingAmount = std::clamp(currentSong->swingAmount + offset, -49, 49);
	if (newSwingAmount != currentSong->swingAmount) {
		actionLogger.recordSwingChange(currentSong->swingAmount, newSwingAmount);
		currentSong->swingAmount = newSwingAmount;
	}
	commandDisplaySwingAmount();
}

void PlaybackHandler::commandNudgeClock(int8_t offset) {
	if (!isEitherClockActive()) {
		// Nothing to nudge
		return;
	}
	if (isInternalClockActive()) {
		if (currentlySendingMIDIOutputClocks()) {
			if (offset < 0) {
				// Send one extra clock
				midiEngine.sendClock(this);
				cvEngine.analogOutTick();
			}
			else if (offset > 0) {
				// Skip one clock
				skipAnalogClocks++;
				skipMidiClocks++;
			}
		}
		else {
			// Nothing to nudge? TODO: Should we instead nudge the internal clock? Could be
			// useful for manual beat syncs.
			return;
		}
	}
	else if (isExternalClockActive()) {
		if (offset < 0) {
			inputTick(); // Perform extra tick
		}
		else {
			numInputTicksToSkip++; // Perform one less tick
		}
	}
	display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_SYNC_NUDGED));
}

void PlaybackHandler::commandEditClockOutScale(int8_t offset) {
	// If playing synced, double or halve our own playing speed relative to the outside world
	if (isExternalClockActive()) {
		if (offset < 0 || currentSong->mayDoubleTempo()) { // Know when to disallow tempo doubling

			// Get current tempo
			int32_t magnitude;
			int8_t whichValue;
			getCurrentTempoParams(&magnitude, &whichValue);

			magnitude -= offset;

			currentSong->setTempoFromParams(magnitude, whichValue, true);

			// We need to speed up (and resync) relative to incoming clocks
			currentSong->insideWorldTickMagnitude += offset;
			resyncInternalTicksToInputTicks(currentSong);

			// We do not need to call resyncAnalogOutTicksToInternalTicks(). Yes the insideWorldTickMagnitude has
			// changed, but whatever effect this has on the scaling of input ticks to internal ticks, it has the
			// opposite effect on the scaling of internal ticks to analog out ticks, so nothing needs to change
		}
	}

	// Otherwise, change the output-tick scale - if we're actually sending out-ticks
	else {

		currentSong->insideWorldTickMagnitude -= offset;

		if (isInternalClockActive()) {

			// Fix MIDI beat clock output
			if (currentlySendingMIDIOutputClocks()) {
				resyncMIDIClockOutTicksToInternalTicks();
				sendOutPositionViaMIDI(getCurrentInternalTickCount());
			}

			// Fix analog out clock
			resyncAnalogOutTicksToInternalTicks();
		}

		commandDisplayTempo();
	}
}

void PlaybackHandler::commandEditTempoCoarse(int8_t offset) {
	// Get current tempo
	int32_t magnitude;
	int8_t whichValue;
	getCurrentTempoParams(&magnitude, &whichValue);

	whichValue += offset;

	if (whichValue >= 16) {
		whichValue -= 16;
		magnitude--;
	}
	else if (whichValue < 0) {
		whichValue += 16;
		magnitude++;
	}

	currentSong->setTempoFromParams(magnitude, whichValue, true);

	displayTempoFromParams(magnitude, whichValue);
}

void PlaybackHandler::commandEditTempoFine(int8_t offset) {
	int32_t tempoBPM = calculateBPM(currentSong->getTimePerTimerTickFloat()) + 0.5f;
	tempoBPM += offset;
	if (tempoBPM > 0) {
		currentSong->setBPM(tempoBPM, true);
		displayTempoBPM(tempoBPM);
	}
}

void PlaybackHandler::tempoEncoderAction(int8_t offset, bool encoderButtonPressed, bool shiftButtonPressed) {

	if (Buttons::isButtonPressed(deluge::hid::button::TAP_TEMPO)) {
		if (display->hasPopupOfType(PopupType::SWING)) {
			// If not yet displaying, don't change the value
			return commandEditSwingInterval(offset);
		}
		else {
			return commandDisplaySwingInterval();
		}
	}

	offset = std::max((int8_t)-1, std::min((int8_t)1, offset));

	// Nudging sync
	if (Buttons::isButtonPressed(deluge::hid::button::X_ENC)) {
		return commandNudgeClock(offset);
	}

	// Adjust swing
	else if (shiftButtonPressed) {
		if (display->hasPopupOfType(PopupType::SWING)) {
			return commandEditSwingAmount(offset);
		}
		else {
			return commandDisplaySwingAmount();
		}
	}

	// If MIDI learn button down, change clock out scale
	else if (Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
		return commandEditClockOutScale(offset);
	}

	// Otherwise, change tempo
	else {
		if (!isExternalClockActive()) {
			UI* currentUI = getCurrentUI();
			bool isOLEDSessionView = display->haveOLED() && (currentUI == &sessionView || currentUI == &arrangerView);
			if (display->hasPopupOfType(PopupType::TEMPO) || isOLEDSessionView) {
				// Truth table for how we decide between adjusting coarse and fine tempo:
				//
				// Setting | Button | Mode
				// -----------------------
				//  On     | Off    | Fine
				//  On     | On     | Coarse
				//  Off    | Off    | Coarse
				//  Off    |  On    | Fine
				//
				bool feature = runtimeFeatureSettings.get(RuntimeFeatureSettingType::FineTempoKnob)
				               == RuntimeFeatureStateToggle::On;
				bool button = Buttons::isButtonPressed(deluge::hid::button::TEMPO_ENC);

				if (feature == button) {
					return commandEditTempoCoarse(offset);
				}
				else {
					return commandEditTempoFine(offset);
				}
			}
			else {
				commandDisplayTempo();
			}
		}
	}
}

void PlaybackHandler::sendOutPositionViaMIDI(int32_t pos, bool sendContinueMessageToo) {
	uint32_t newOutputTicksDone = timerTicksToOutputTicks(pos);
	uint32_t positionPointer = newOutputTicksDone / 6;
	int32_t surplusOutputTicks = (newOutputTicksDone % 6);

	// Or if position pointer too big to fit into 14-bit number...
	if (positionPointer >= 16384) {

		if (currentPlaybackMode == &session) {
			Clip* longestClip = currentSong->getLongestClip(false, true);
			// In rare case of no play-enabled Clip, we'll just have to send position-0
			if (!longestClip) {
				positionPointer = 0;
				surplusOutputTicks = 0;
			}

			// Or, more normally, get the position from our current play-position within the longest Clip
			else {
				// Have to do a fair bit of working-backwards to get output clock ticks from Song ticks
				uint32_t internalTicks = longestClip->getActualCurrentPosAsIfPlayingInForwardDirection();
				newOutputTicksDone = timerTicksToOutputTicks(internalTicks);
				positionPointer = newOutputTicksDone / 6;
				surplusOutputTicks = newOutputTicksDone % 6;
			}
		}

		else {
			positionPointer &= 16383;
		}
	}

	surplusOutputTicks++; // Need one extra one to make the follower "play" the position the pointer points to, right
	                      // now.

	midiEngine.sendPositionPointer(this, positionPointer);

	// If we've just switched output clocks back on and they were off, we'd better send a continue message
	// BUT this didn't work in Ableton! In Live 8, you could instead put a "start" message *before* sending the position
	// pointer. But this doesn't work anymore in Live 9, so let's just do it the "proper" MIDI way always.
	if (sendContinueMessageToo) {
		midiEngine.sendContinue(this);
	}

	for (int32_t i = 0; i < surplusOutputTicks; i++) {
		midiEngine.sendClock(this, i != 0);
	}
}

void PlaybackHandler::setMidiOutClockMode(bool newValue) {
	if (newValue == midiOutClockEnabled) {
		return;
	}
	int32_t oldValue = midiOutClockEnabled;
	midiOutClockEnabled = newValue;

	// If currently playing on internal clock...
	if (isInternalClockActive()) {

		// If we just enabled the output clock...
		if (!oldValue) {
			resyncMIDIClockOutTicksToInternalTicks();
			sendOutPositionViaMIDI(getCurrentInternalTickCount(), true);
		}

		// Or if we just disabled it...
		else if (!newValue) {
			midiClockOutTickScheduled = false;
			midiEngine.sendStop(this);
		}
	}
}

void PlaybackHandler::setMidiInClockEnabled(bool newValue) {
	if (newValue == midiInClockEnabled) {
		return;
	}
	midiInClockEnabled = newValue;

	// If currently playing synced...
	if (!newValue && isExternalClockActive() && !usingAnalogClockInput) {
		endPlayback();
	}
}

void PlaybackHandler::getCurrentTempoParams(int32_t* magnitude, int8_t* whichValue) {
	*magnitude = 0;
	uint64_t timePer = currentSong->timePerTimerTickBig;
	while (timePer > ((uint64_t)metronomeValueBoundaries[15] << (1 + 32))) {
		timePer >>= 1;
		(*magnitude)++;
	}

	while (timePer <= ((uint64_t)metronomeValueBoundaries[15] << 32)) {
		timePer <<= 1;
		(*magnitude)--;
	}

	*whichValue = 15;

	for (int32_t i = 0; i < 16; i++) {
		if (timePer > ((uint64_t)metronomeValueBoundaries[i] << 32)) {
			*whichValue = i;
			break;
		}
	}
}

void PlaybackHandler::displayTempoFromParams(int32_t magnitude, int8_t whichValue) {
	float tempoBPM = metronomeValuesBPM[whichValue];
	magnitude += currentSong->insideWorldTickMagnitude + currentSong->insideWorldTickMagnitudeOffsetFromBPM;
	if (magnitude > 0) {
		tempoBPM /= ((uint32_t)1 << magnitude);
	}
	else if (magnitude < 0) {
		tempoBPM *= ((uint32_t)1 << (0 - magnitude));
	}
	displayTempoBPM(tempoBPM);
}

void PlaybackHandler::commandDisplayTempo() {
	float bpm = calculateBPMForDisplay();
	displayTempoBPM(bpm);
}
// takes into account incoming clock sync
float PlaybackHandler::calculateBPMForDisplay() {
	return calculateBPM(getTimePerInternalTickFloat());
}

float PlaybackHandler::calculateBPM(float timePerInternalTick) {
	return currentSong->calculateBPM(timePerInternalTick);
}

void PlaybackHandler::getTempoStringForOLED(float tempoBPM, StringBuf& buffer) {
	if (tempoBPM >= 9999.5) {
		buffer.append("FAST");
	}
	else {
		int32_t numDecimalPlaces = (tempoBPM >= 1000 || isExternalClockActive()) ? 0 : 2;
		buffer.appendFloat(tempoBPM, 0, numDecimalPlaces);
	}
}

void PlaybackHandler::displayTempoBPM(float tempoBPM) {
	// The 7-seg needs to work so much harder there's no point trying to share the code.
	DEF_STACK_STRING_BUF(text, 27);
	if (display->haveOLED()) {
		UI* currentUI = getCurrentUI();
		// if we're currently in song or arranger view, we'll render tempo on the display instead of a popup
		if ((currentUI == &sessionView || currentUI == &arrangerView)
		    && !deluge::hid::display::OLED::isPermanentPopupPresent()) {
			sessionView.lastDisplayedTempo = tempoBPM;
			getTempoStringForOLED(tempoBPM, text);
			sessionView.displayTempoBPM(deluge::hid::display::OLED::main, text, true);
			deluge::hid::display::OLED::markChanged();
		}
		else {
			text.append("Tempo: ");
			getTempoStringForOLED(tempoBPM, text);
			display->popupTextTemporary(text.c_str(), PopupType::TEMPO);
		}
	}
	else {
		if (tempoBPM >= 9999.5) {
			return display->popupTextTemporary("FAST", PopupType::TEMPO);
		}

		int32_t divisor = 1;
		int32_t dotMask = (1 << 7);

		if (tempoBPM >= 999.95) {}
		else if (tempoBPM >= 99.995) {
			divisor = 10;
			dotMask |= (1 << 1);
		}
		else if (tempoBPM >= 9.9995) {
			divisor = 100;
			dotMask |= (1 << 2);
		}
		else {
			divisor = 1000;
			dotMask |= (1 << 3);
		}

		int32_t roundedBigger = tempoBPM * divisor + 0.5; // This will now be a 4 digit number
		double roundedSmallerAgain = (double)roundedBigger / divisor;

		bool isPerfect = false;

		// It might get supplied here as a 0, even if it's actually very slightly above that, but we don't want do
		// display that as an integer
		if (roundedBigger != 0 && !isExternalClockActive()) {

			// Compare to current tempo to see if 100% accurate
			double roundedSmallerHere = roundedSmallerAgain;
			if (currentSong->insideWorldTickMagnitude > 0) {
				roundedSmallerHere *= ((uint32_t)1 << (currentSong->insideWorldTickMagnitude));
			}
			double newTempoSamples = (double)110250 / roundedSmallerHere;
			if (currentSong->insideWorldTickMagnitude < 0) {
				newTempoSamples *= ((uint32_t)1 << (-currentSong->insideWorldTickMagnitude));
			}

			uint64_t newTimePerTimerTickBig = newTempoSamples * 4294967296 + 0.5;

			isPerfect = (currentSong->timePerTimerTickBig == newTimePerTimerTickBig);
		}

		int32_t roundedTempoBPM = roundedSmallerAgain + 0.5;

		// If perfect and integer...
		if (isPerfect && roundedBigger == roundedTempoBPM * divisor) {
			text.appendInt(roundedTempoBPM);
			display->popupTextTemporary(text.c_str(), PopupType::TEMPO);
		}
		else {
			text.appendInt(roundedBigger, 4);
			// This is what popupTextTemporary() does, except for passing in the dotMask
			display->displayPopup(text.c_str(), 3, false, dotMask, 1, PopupType::TEMPO);
		}
	}
}

void PlaybackHandler::setLedStates() {

	indicator_leds::setLedState(IndicatorLED::PLAY, playbackState);

	if (audioRecorder.recordingSource < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION
	    && recording != RecordingMode::ARRANGEMENT) {
		indicator_leds::setLedState(IndicatorLED::RECORD, recording == RecordingMode::NORMAL);
	}

	bool syncedLEDOn = isExternalClockActive();
	setOutputState(SYNCED_LED.port, SYNCED_LED.pin, syncedLEDOn);

	indicator_leds::setLedState(IndicatorLED::TAP_TEMPO, metronomeOn);
}

void PlaybackHandler::toggleMetronomeStatus() {
	metronomeOn = !metronomeOn;
	setLedStates();
	if (isEitherClockActive() && metronomeOn) {
		expectEvent();
	}
}

// Called when playing synced and sync scaling or magnitude have been changed - e.g. when user doubles or halves tempo,
// or sync scaling is activated
void PlaybackHandler::resyncInternalTicksToInputTicks(Song* song) {
	if (isExternalClockActive()) {
		// This works. Although it doesn't do anything special to account for swing, no long-term out-of-sync-ness
		// results - I tested.
		lastSwungTickActioned = getCurrentInternalTickFloatFollowingExternalClock();
		currentPlaybackMode->resyncToSongTicks(song);

		// In most cases, if we're here, we'll want to alter the "following" internal tick tempo to "remember" what
		// time-scaling stuff we changed. This happens routinely every ~24 clocks anyway, but sometimes it makes sense
		// to store this change instantly, e.g. if we just changed time-scaling like we probably just did
		resetTimePerInternalTickMovingAverage();
	}
}

// This is that special MIDI command for Todd T
void PlaybackHandler::forceResetPlayPos(Song* song, bool restartingPlayback) {
	if (playbackState) {

		endPlayback();

		if (isExternalClockActive()) {
			setupPlaybackUsingExternalClock();
		}

		else {
			setupPlaybackUsingInternalClock(0, false, restartingPlayback);
		}
	}
}

void PlaybackHandler::grabTempoFromClip(Clip* clip) {

	if (clip->type != ClipType::AUDIO || clip->getCurrentlyRecordingLinearly()
	    || !((AudioClip*)clip)->sampleHolder.audioFile) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_GRAB_TEMPO_FROM_CLIP));
		return;
	}

	uint64_t loopLengthSamples = ((AudioClip*)clip)->sampleHolder.getLengthInSamplesAtSystemSampleRate(true);
	Action* action = actionLogger.getNewAction(ActionType::TEMPO_CHANGE);

	// setTempoFromAudioClipLength(loopLengthSamples, action);

	double timePerTick = (double)loopLengthSamples / clip->loopLength;

	uint64_t timePerBigBefore = currentSong->timePerTimerTickBig;

	currentSong->setTempoFromNumSamples(timePerTick, false);

	// Record that change, ourselves. We sent false above because that mechanism of recording it would do all this other
	// stuff
	if (action) {

		void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceTempoChange));

		if (consMemory) {
			ConsequenceTempoChange* newConsequence =
			    new (consMemory) ConsequenceTempoChange(timePerBigBefore, currentSong->timePerTimerTickBig);
			action->addConsequence(newConsequence);
		}
	}

	commandDisplayTempo();
}

uint32_t PlaybackHandler::setTempoFromAudioClipLength(uint64_t loopLengthSamples, Action* action) {

	uint32_t ticksLong = 3;

	float timePerTick;

	uint32_t timePerTimerTick = currentSong->getTimePerTimerTickRounded();

	while (true) {
		timePerTick = (float)loopLengthSamples / ticksLong;
		if (timePerTick < timePerTimerTick * 1.41) {
			break;
		}
		ticksLong <<= 1;
	}
	uint64_t timePerBigBefore = currentSong->timePerTimerTickBig;

	currentSong->setTempoFromNumSamples(timePerTick, false);

	// Record that change, ourselves. We sent false above because that mechanism of recording it would do all this other
	// stuff
	if (action) {

		void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceTempoChange));

		if (consMemory) {
			ConsequenceTempoChange* newConsequence =
			    new (consMemory) ConsequenceTempoChange(timePerBigBefore, currentSong->timePerTimerTickBig);
			action->addConsequence(newConsequence);
		}
	}

	commandDisplayTempo();

	return ticksLong;
}

void PlaybackHandler::finishTempolessRecording(bool shouldStartPlaybackAgain, int32_t buttonLatencyForTempolessRecord,
                                               bool shouldExitRecordMode) {

	bool foundAnyYet = false;
	uint32_t ticksLong = 3;

	Action* action = NULL;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	for (int32_t c = 0; c < currentSong->sessionClips.getNumElements(); c++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

		if (clip->getCurrentlyRecordingLinearly()) {
			ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

			Clip* nextPendingOverdub = currentSong->getPendingOverdubWithOutput(clip->output);

			clip->finishLinearRecording(modelStackWithTimelineCounter, NULL,
			                            buttonLatencyForTempolessRecord); // nextPendingOverdub doesn't actually get
			                                                              // used in this call, for audioClips

			// If first one found, calculate tempo and everything
			if (!foundAnyYet) {
				SampleHolder* sampleHolder = &((AudioClip*)clip)->sampleHolder;
				if (!sampleHolder->audioFile) {
					continue; // Could maybe happen if some error?
				}

				foundAnyYet = true;
				uint64_t loopLengthSamples = sampleHolder->getDurationInSamples(true);
				action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);

				ticksLong = setTempoFromAudioClipLength(loopLengthSamples, action);
			}

			// Set length, if different
			if (clip->loopLength != ticksLong) {

				uint32_t oldLength = clip->loopLength;
				clip->loopLength = ticksLong;

				action->recordClipLengthChange(clip, oldLength);
			}

			clip->originalLength =
			    ticksLong; // Reset this. After tempoless recording, actual original length is irrelevant

			if (nextPendingOverdub) {
				nextPendingOverdub->copyBasicsFrom(clip); // Copy this again, in case it's changed since it was created
			}
		}
	}

	// Used to call endPlayback() here, but it actually isn't needed
	if (!shouldStartPlaybackAgain) {
		endPlayback();
	}

	if (shouldExitRecordMode && recording == RecordingMode::NORMAL) {
		recording = RecordingMode::OFF;
		setLedStates();
	}

	// Unless the user just hit the play button to stop playback, we probably just want it to start again - so they hear
	// the loop they just recorded
	if (shouldStartPlaybackAgain) {

		// And remember that this tempoless-record Action included beginning playback, so undoing / redoing it later
		// will stop and start playback respectively
		if (action) {
			void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceBeginPlayback));

			if (consMemory) {
				ConsequenceBeginPlayback* newConsequence = new (consMemory) ConsequenceBeginPlayback();
				action->addConsequence(newConsequence);
			}
		}

		setupPlaybackUsingInternalClock(0,
		                                false); // Send 0 for buttonPressLatency - cos we know that another tempoless
		                                        // record is not about to begin, cos we just finished one
	}
}

static const uint32_t noteRecordingUIModes[] = {UI_MODE_HORIZONTAL_ZOOM, UI_MODE_HORIZONTAL_SCROLL, UI_MODE_AUDITIONING,
                                                UI_MODE_RECORD_COUNT_IN, 0};

bool PlaybackHandler::shouldRecordNotesNow() {
	return (isEitherClockActive() && recording != RecordingMode::OFF && isUIModeWithinRange(noteRecordingUIModes)
	        && (!playbackHandler.ticksLeftInCountIn
	            || getTimeLeftInCountIn()
	                   <= kLinearRecordingEarlyFirstNoteAllowance) // If doing a count-in, only allow notes to be
	                                                               // recorded up to 100mS early
	);
}

int32_t PlaybackHandler::getTimeLeftInCountIn() {
	uint32_t remainder;
	uint32_t ticks =
	    playbackHandler.ticksLeftInCountIn - playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick(&remainder);
	int32_t timeLeft = ticks * playbackHandler.getTimePerInternalTick() - remainder;
	if (timeLeft < 0) {
		timeLeft = 0;
	}
	return timeLeft;
}

void PlaybackHandler::stopAnyRecording() {
	if (playbackState && recording != RecordingMode::OFF) {
		bool wasRecordingArrangement = (recording == RecordingMode::ARRANGEMENT);
		recording = RecordingMode::OFF;

		setLedStates();
		if (wasRecordingArrangement) {
			view.setModLedStates();
		}
	}
}

void PlaybackHandler::tapTempoButtonPress() {
	if (tapTempoNumPresses == 0) {
		tapTempoFirstPressTime = AudioEngine::audioSampleTimer;
	}
	else {
		uint64_t totalTimeBetweenBig = (uint64_t)(uint32_t)(AudioEngine::audioSampleTimer - tapTempoFirstPressTime)
		                               << 32;
		uint64_t timePerQuarterNoteBig = totalTimeBetweenBig / tapTempoNumPresses;

		int16_t magnitudeChange = currentSong->insideWorldTickMagnitude + 3;

		if (magnitudeChange >= 0) {
			timePerQuarterNoteBig >>= magnitudeChange;
		}
		else {
			timePerQuarterNoteBig <<= (-magnitudeChange);
		}
		// timePerQuarterNote no longer represents quarter notes, but 3-ticks's

		actionLogger.closeAction(ActionType::TEMPO_CHANGE); // Don't add to previous action
		currentSong->setTimePerTimerTick(
		    timePerQuarterNoteBig / 3,
		    true); // Put the fraction in the middle; it's more likely to be accurate since we've been rounding down
		actionLogger.closeAction(ActionType::TEMPO_CHANGE); // Don't allow next action to add to this one

		commandDisplayTempo();
	}
	tapTempoNumPresses++;

	indicator_leds::blinkLed(IndicatorLED::TAP_TEMPO, 255, 1);

	uiTimerManager.setTimer(TimerName::TAP_TEMPO_SWITCH_OFF, 1100);
}

// Returns whether the message has been used up by a command
bool PlaybackHandler::tryGlobalMIDICommands(MIDIDevice* device, int32_t channel, int32_t note) {

	bool foundAnything = false;

	for (int32_t c = 0; c < kNumGlobalMIDICommands; c++) {
		GlobalMIDICommand command = static_cast<GlobalMIDICommand>(c);

		if (midiEngine.globalMIDICommands[c].equalsChannelOrZone(device, channel)
		    && command == GlobalMIDICommand::TRANSPOSE) {
			foundAnything = true;
			MIDITranspose::doTranspose(true, note);
		}

		else if (midiEngine.globalMIDICommands[c].equalsNoteOrCC(device, channel, note)) {
			switch (command) {
			case GlobalMIDICommand::PLAYBACK_RESTART:
				if (recording != RecordingMode::ARRANGEMENT) {
					forceResetPlayPos(currentSong, true);
				}
				break;

			case GlobalMIDICommand::PLAY:
				playButtonPressed(kMIDIKeyInputLatency);
				break;

			case GlobalMIDICommand::RECORD:
				recordButtonPressed();
				break;

			case GlobalMIDICommand::LOOP:
			case GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING:
				tryLoopCommand(command);
				break;

			case GlobalMIDICommand::REDO:
			case GlobalMIDICommand::UNDO:
				if (actionLogger.allowedToDoReversion()) {
					// We're going to "pend" it rather than do it right now in any case.
					// Firstly, we don't want to do it while we may be in some card access routine - e.g. by a
					// SampleRecorder. Secondly, reversion can take a lot of time, and may want to call the audio
					// routine - which is locked cos we're in it!
					pendingGlobalMIDICommand = command;
					pendingGlobalMIDICommandNumClustersWritten = 0;
				}
				break;

			case GlobalMIDICommand::FILL:
				currentSong->changeFillMode(true);
				break;

			// case GlobalMIDICommand::TAP:
			default:
				if (getCurrentUI() == getRootUI()) {
					if (currentUIMode == UI_MODE_NONE) {
						tapTempoButtonPress();
					}
				}
				break;
			}

			foundAnything = true;
		}
	}

	return foundAnything;
}

void PlaybackHandler::tryLoopCommand(GlobalMIDICommand command) {
	if (actionLogger.allowedToDoReversion()
	    // Not quite sure if this describes exactly what we want but it'll do...
	    || currentUIMode == UI_MODE_RECORD_COUNT_IN) {
		OverDubType overdubNature =
		    (command == GlobalMIDICommand::LOOP) ? OverDubType::Normal : OverDubType::ContinuousLayering;
		loopCommand(overdubNature);
	}
}

// Returns whether the message has been used up by a note-off command
bool PlaybackHandler::tryGlobalMIDICommandsOff(MIDIDevice* device, int32_t channel, int32_t note) {

	bool foundAnything = false;

	if (midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::TRANSPOSE)].equalsChannelOrZone(device,
	                                                                                                         channel)) {
		foundAnything = true;
		MIDITranspose::doTranspose(false, note);
	}
	else {
		// Check for FILL command at index [8]
		if (midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::FILL)].equalsNoteOrCC(device, channel,
		                                                                                               note)) {
			currentSong->changeFillMode(false);
			foundAnything = true;
		}
	}

	return foundAnything;
}

void PlaybackHandler::programChangeReceived(MIDIDevice* fromDevice, int32_t channel, int32_t program) {
	// If user assigning MIDI commands, do that
	if (currentUIMode == UI_MODE_MIDI_LEARN) {
		if (getCurrentUI()->pcReceivedForMidiLearn(fromDevice, channel, program)) {}
		else {
			view.pcReceivedForMIDILearn(fromDevice, channel, program);
		}
	}
	else {
		// we build ontop of the CC hack
		offerNoteToLearnedThings(fromDevice, true, channel + IS_A_PC, program);
	}
}
bool PlaybackHandler::offerNoteToLearnedThings(MIDIDevice* fromDevice, bool on, int32_t channel, int32_t note) {

	// Otherwise, enact the relevant MIDI command, if it can be found

	bool foundAnything = false;

	// Check global function commands - Off variant checks note off for momentary commands
	if (on) {
		foundAnything = tryGlobalMIDICommands(fromDevice, channel, note);
	}
	else {
		foundAnything = tryGlobalMIDICommandsOff(fromDevice, channel, note);
	}

	// Go through all sections
	for (int32_t s = 0; s < kMaxNumSections; s++) {
		if (currentSong->sections[s].launchMIDICommand.equalsNoteOrCC(fromDevice, channel, note)) {
			if (on) {
				if (arrangement.hasPlaybackActive()) {
					switchToSession();
				}
				session.armSection(s, kMIDIKeyInputLatency);
			}
			foundAnything = true;
		}
	}

	// Go through all Clips in session only
	for (int32_t c = currentSong->sessionClips.getNumElements() - 1; c >= 0; c--) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(c);

		// Mute action on Clip?
		if (clip->muteMIDICommand.equalsNoteOrCC(fromDevice, channel, note)) {
			if (on) {

				if (arrangement.hasPlaybackActive()) {
					switchToSession();
				}

				// Beware - calling this might insert or delete a Clip!
				session.toggleClipStatus(clip, &c, false, kMIDIKeyInputLatency);

				// use root UI in case this is called from performance view
				sessionView.requestRendering(getRootUI(), 0, 0xFFFFFFFF);
			}
			foundAnything = true;
		}
	}

	return foundAnything;
}

void PlaybackHandler::noteMessageReceived(MIDIDevice* fromDevice, bool on, int32_t channel, int32_t note,
                                          int32_t velocity, bool* doingMidiThru) {
	// If user assigning/learning MIDI commands, do that
	if (currentUIMode == UI_MODE_MIDI_LEARN && on) {
		// Checks velocity to let note-offs pass through,
		// so no risk of stuck note if they pressed learn while holding a note
		int32_t channelOrZone = fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].channelToZone(channel);

		if (getCurrentUI()->noteOnReceivedForMidiLearn(fromDevice, channelOrZone, note, velocity)) {}
		else {
			view.noteOnReceivedForMidiLearn(fromDevice, channelOrZone, note, velocity);
		}
		return;
	}

	// Otherwise, enact the relevant MIDI command, if it can be found

	if (offerNoteToLearnedThings(fromDevice, on, channel, note)) {
		return;
	}

	bool shouldRecordNotesNowNow = shouldRecordNotesNow();

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	// See if note message received should be processed by midi follow mode
	midiFollow.noteMessageReceived(fromDevice, on, channel, note, velocity, doingMidiThru, shouldRecordNotesNowNow,
	                               modelStack);

	// Go through all Instruments...
	for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {

		// Only send if not muted - but let note-offs through always, for safety
		if (!on || currentSong->isOutputActiveInArrangement(thisOutput)) {

			ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
			    modelStack->addTimelineCounter(thisOutput->getActiveClip());
			// Output is a MIDI instrument, kit, or melodic instrument
			// Midi instruments will hand control to NonAudioInstrument which inherits from melodic
			thisOutput->offerReceivedNote(modelStackWithTimelineCounter, fromDevice, on, channel, note, velocity,
			                              shouldRecordNotesNowNow
			                                  && currentSong->isOutputActiveInArrangement(
			                                      thisOutput), // Definitely don't record if muted in arrangement
			                              doingMidiThru);
		}
	}
}

void PlaybackHandler::expectEvent() {
	if (!currentlyActioningSwungTickOrResettingPlayPos && isEitherClockActive()) {
		int32_t newSwungTicksTilNextEvent = getNumSwungTicksInSinceLastActionedSwungTick() + 1;
		if (newSwungTicksTilNextEvent < swungTicksTilNextEvent) {
			swungTicksTilNextEvent = newSwungTicksTilNextEvent;
			scheduleSwungTick();
		}
	}
}

bool PlaybackHandler::subModeAllowsRecording() {
	return (currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_HORIZONTAL_ZOOM
	        || currentUIMode == UI_MODE_HORIZONTAL_SCROLL);
}

bool PlaybackHandler::isCurrentlyRecording() {
	return (playbackState && recording != RecordingMode::OFF);
}

void PlaybackHandler::switchToArrangement() {
	currentPlaybackMode = &arrangement;
	stopOutputRecordingAtLoopEnd = false;
	session.endPlayback();
	arrangement.setupPlayback();
	arrangement.resetPlayPos(arrangementPosToStartAtOnSwitch);
	arrangerView.reassessWhetherDoingAutoScroll();
	if (display->haveOLED()) {
		if (!isUIModeActive(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW)
		    && !isUIModeActive(UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
			renderUIsForOled();
		}
	}
	else {
		sessionView.redrawNumericDisplay();
	}

	if (getCurrentUI() == &sessionView) {
		PadLEDs::reassessGreyout();
	}
}

void PlaybackHandler::switchToSession() {
	if (recording != RecordingMode::OFF) {
		arrangement.endAnyLinearRecording();
	}
	currentPlaybackMode = &session;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);

	currentSong->paramManager.expectNoFurtherTicks(modelStack);

	session.setupPlayback();
	stopOutputRecordingAtLoopEnd = false;

	if (getCurrentUI() == &sessionView) {
		PadLEDs::reassessGreyout();
	}
}

bool dealingWithReceivedMIDIPitchBendRightNow = false;

void PlaybackHandler::pitchBendReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2,
                                        bool* doingMidiThru) {

	bool isMPE = fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].isChannelPartOfAnMPEZone(channel);

	if (isMPE) {
		fromDevice->defaultInputMPEValuesPerMIDIChannel[channel][0] =
		    (((uint32_t)data1 | ((uint32_t)data2 << 7)) - 8192) << 2;
	}
	else {
		// If the SoundEditor is the active UI, give it first dibs on the message
		if (getCurrentUI() == &soundEditor) {
			if (soundEditor.pitchBendReceived(fromDevice, channel, data1, data2)) {
				return;
			}
		}
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	dealingWithReceivedMIDIPitchBendRightNow = true;

	// See if pitch bend received should be processed by midi follow mod
	midiFollow.pitchBendReceived(fromDevice, channel, data1, data2, doingMidiThru, modelStack);

	// Go through all Outputs...
	for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
		    modelStack->addTimelineCounter(thisOutput->getActiveClip());

		bool usedForParam = false;

		if (!isMPE && modelStackWithTimelineCounter->timelineCounterIsSet()) { // Do we still need to check this?
			// See if it's learned to a parameter
			usedForParam = thisOutput->offerReceivedPitchBendToLearnedParams(
			    fromDevice, channel, data1, data2,
			    modelStackWithTimelineCounter); // NOTE: this call may change
			                                    // modelStackWithTimelineCounter->timelineCounter etc!
		}

		if (!usedForParam) {
			thisOutput->offerReceivedPitchBend(modelStackWithTimelineCounter, fromDevice, channel, data1, data2,
			                                   doingMidiThru);
		}
	}

	dealingWithReceivedMIDIPitchBendRightNow = false;
}

void PlaybackHandler::midiCCReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value,
                                     bool* doingMidiThru) {
	// true only if it's an MPE member channel, and therefore only used for per note expression
	bool isMPE = fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].isChannelPartOfAnMPEZone(channel);

	if (isMPE) {
		fromDevice->defaultInputMPEValuesPerMIDIChannel[channel][1] = (value - 64) << 9;
	}
	else {
		int32_t channelOrZone = fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].channelToZone(channel);
		// If the SoundEditor is the active UI, give it first dibs on the message
		if (getCurrentUI() == &soundEditor) {
			if (soundEditor.midiCCReceived(fromDevice, channelOrZone, ccNumber, value)) {
				return;
			}
		}
		// then midi learn is second priority
		else if (currentUIMode == UI_MODE_MIDI_LEARN) {
			view.ccReceivedForMIDILearn(fromDevice, channelOrZone, ccNumber, value);
			// we don't want this learn to immediately trigger the thing it was learnt to so just return
			return;
		}
		// check if it was learned to on/off commands (loop, drums, section launch etc.)
		else if (offerNoteToLearnedThings(fromDevice, value > 0, channelOrZone + IS_A_CC, ccNumber)) {
			return;
		}
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	// See if midi cc received should be processed by midi follow mode
	midiFollow.midiCCReceived(fromDevice, channel, ccNumber, value, doingMidiThru, modelStack);

	// See if midi cc received has been learned to a song param
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
	if (modelStackWithThreeMainThings) {
		ModControllableAudio* modControllable = (ModControllableAudio*)modelStackWithThreeMainThings->modControllable;
		if (modControllable) {
			modControllable->offerReceivedCCToLearnedParamsForSong(fromDevice, channel, ccNumber, value,
			                                                       modelStackWithThreeMainThings);
		}
	}

	// Go through all Outputs...
	for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {

		// If it has an activeClip... (Hmm, interesting, we don't allow MIDI control of params when no activeClip? Yeah
		// this checks out, as the various offerReceivedCCToLearnedParams()'s require a timelineCounter, but this seems
		// restrictive for the user...)
		if (thisOutput->getActiveClip()) {

			ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
			    modelStack->addTimelineCounter(thisOutput->getActiveClip());

			if (!isMPE) {
				// See if it's learned to a parameter
				// NOTE: this call may change modelStackWithTimelineCounter->timelineCounter etc!
				thisOutput->offerReceivedCCToLearnedParams(fromDevice, channel, ccNumber, value,
				                                           modelStackWithTimelineCounter);
			}

			thisOutput->offerReceivedCC(modelStackWithTimelineCounter, fromDevice, channel, ccNumber, value,
			                            doingMidiThru);
		}
	}
}

// noteCode -1 means channel-wide, including for MPE input (which then means it could still then just apply to one
// note).
void PlaybackHandler::aftertouchReceived(MIDIDevice* fromDevice, int32_t channel, int32_t value, int32_t noteCode,
                                         bool* doingMidiThru) {

	bool isMPE =
	    (noteCode == -1 && fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].isChannelPartOfAnMPEZone(channel));

	if (isMPE) {
		fromDevice->defaultInputMPEValuesPerMIDIChannel[channel][2] = value << 8;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);

	// See if aftertouch received should be processed by midi follow mode
	midiFollow.aftertouchReceived(fromDevice, channel, value, noteCode, doingMidiThru, modelStack);

	// Go through all Instruments...
	for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {

		ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
		    modelStack->addTimelineCounter(thisOutput->getActiveClip());

		thisOutput->offerReceivedAftertouch(modelStackWithTimelineCounter, fromDevice, channel, value, noteCode,
		                                    doingMidiThru);
	}
}

int32_t PlaybackHandler::getActualArrangementRecordPos() {
	return getActualSwungTickCount() + arrangement.playbackStartedAtPos;
}

int32_t PlaybackHandler::getArrangementRecordPosAtLastActionedSwungTick() {
	return lastSwungTickActioned + arrangement.playbackStartedAtPos;
}

// Warning - this might get called during card routine!
void PlaybackHandler::loopCommand(OverDubType overdubNature) {
	bool anyGotArmedToStop;
	bool mustEndTempolessRecordingAfter = false;

	// If not playing yet, start
	if (!playbackState) {
		if (recording == RecordingMode::OFF) {
			recording = RecordingMode::NORMAL;
		}
		playButtonPressed(kMIDIKeyInputLatency);
	}

	// Or, if doing count-in, stop that and playback altogether
	else if (ticksLeftInCountIn) {
		endPlayback();

probablyExitRecordMode:
		// Exit RECORD mode, as indicated on LED
		if (playbackHandler.recording == RecordingMode::NORMAL) {
			playbackHandler.recording = RecordingMode::OFF;
			playbackHandler.setLedStates();
		}
	}

	// Or if in arranger, do nothing
	else if (currentPlaybackMode == &arrangement) {}

	// Or if recording from session to arrangement, we're not allowed to do any loopy stuff during that
	else if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {}

	// Or if tempoless recording, close that
	else if (!isEitherClockActive()) {

		mustEndTempolessRecordingAfter = true;

		// And if LAYERING command, make an overdub too
		if (overdubNature == OverDubType::ContinuousLayering) {
			goto doCreateNextOverdub;
		}
	}

	// If pending overdubs already exist, then delete those.
	else if (currentSong->deletePendingOverdubs()) {
		// TODO: this traverses all Clips. So does further down. This could be combined
		session.launchSchedulingMightNeedCancelling();
		// use root UI in case this is called from performance view
		sessionView.requestRendering(getRootUI());

		goto probablyExitRecordMode;
	}

	else {

		// If any Clips are currently already recording, arm them to stop
		anyGotArmedToStop = false;

		// For each Clip in session
		for (int32_t c = currentSong->sessionClips.getNumElements() - 1; c >= 0; c--) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(c);
			if (clip->armState == ArmState::OFF && clip->getCurrentlyRecordingLinearly()) {
				anyGotArmedToStop = true;
				session.toggleClipStatus(clip, &c, false, kMIDIKeyInputLatency);
			}
		}

		// Or if none were recording, or if it was the LAYERING command, then create a new overdub (potentially in
		// addition to having armed the old one to stop
		if (!anyGotArmedToStop || overdubNature == OverDubType::ContinuousLayering) {

doCreateNextOverdub:

			Clip* clipToCreateOverdubFrom = NULL;
			int32_t clipIndexToCreateOverdubFrom;

			// If we're holding down a Clip in Session View, prioritize that
			if (getRootUI() == &sessionView && currentUIMode == UI_MODE_CLIP_PRESSED_IN_SONG_VIEW) {
				clipToCreateOverdubFrom = sessionView.getClipOnScreen(sessionView.selectedClipPressYDisplay);
				clipIndexToCreateOverdubFrom = sessionView.selectedClipPressYDisplay + currentSong->songViewYScroll;
				sessionView.performActionOnPadRelease = false;
			}

			// Otherwise, prioritize the currentClip - so long as it's not arrangement-only
			else if (getCurrentClip() && !getCurrentClip()->isArrangementOnlyClip()) {
				clipToCreateOverdubFrom = getCurrentClip();
				clipIndexToCreateOverdubFrom = currentSong->sessionClips.getIndexForClip(clipToCreateOverdubFrom);
			}

			// If we've decided which Clip to create overdub from...
			if (clipToCreateOverdubFrom) {

				// So long as it's got an input source...
				if (clipToCreateOverdubFrom->type != ClipType::AUDIO
				    || ((AudioOutput*)clipToCreateOverdubFrom->output)->inputChannel > AudioInputChannel::NONE) {

					// If that Clip wasn't armed to record linearly...
					if (!clipToCreateOverdubFrom->armedForRecording) {

						// Ron's suggestion - just exit out of record mode.
						if (recording != RecordingMode::OFF) {
							recording = RecordingMode::OFF;
						}

						// Or if we weren't in record mode... heck, who knows, maybe just switch it back on?
						else {
							recording = RecordingMode::NORMAL;
						}
						setLedStates();
					}

					// Or if that Clip was armed to record linearly...
					else {
						// ensure that audio clips will clone outputs to layer continuously
						if (overdubNature == OverDubType::ContinuousLayering) {
							clipToCreateOverdubFrom->overdubsShouldCloneOutput = true;
						}

						if (recording == RecordingMode::OFF) {
							recording = RecordingMode::NORMAL;
							setLedStates();
						}

						Clip* overdub = currentSong->createPendingNextOverdubBelowClip(
						    clipToCreateOverdubFrom, clipIndexToCreateOverdubFrom, overdubNature);
						if (overdub) {
							session.scheduleOverdubToStartRecording(overdub, clipToCreateOverdubFrom);
						}
					}
				}
				else {
					display->displayPopup(
					    deluge::l10n::get(deluge::l10n::String::STRING_FOR_AUDIO_TRACK_HAS_NO_INPUT_CHANNEL));
				}
			}
			else {
				display->displayPopup(
				    deluge::l10n::get(deluge::l10n::String::STRING_FOR_CREATE_OVERDUB_FROM_WHICH_CLIP));
			}
		}
	}

	if (mustEndTempolessRecordingAfter) {
		bool shouldExitRecordMode = (overdubNature != OverDubType::ContinuousLayering);
		finishTempolessRecording(true, kMIDIKeyInputLatency, shouldExitRecordMode);
	}
}
