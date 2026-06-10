/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#include "io/midi/device_specific/launchpad_extension.h"

#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/palette.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "io/midi/cable_types/usb_common.h"
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/device_specific/launchpad_cable.h"
#include "io/midi/device_specific/launchpad_note_mode.h"
#include "io/midi/device_specific/launchpad_programmer_map.h"
#include "io/midi/device_specific/launchpad_sysex.h"
#include "io/midi/midi_engine.h"
#include "model/action/action_logger.h"
#include "model/midi/message.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"

extern GlobalMIDICommand pendingGlobalMIDICommand;
extern int32_t pendingGlobalMIDICommandNumClustersWritten;

namespace launchpad_extension {

namespace {

ViewMode viewMode = ViewMode::Session;

uint32_t lastPeriodicSyncTime = 0;
uint32_t lastSyncSentTime = 0;
uint32_t lastProgrammerKeepaliveTime = 0;
uint32_t ignoreLaunchpadInputUntilSample = 0;
bool pendingDelugeSessionView = false;
bool pendingDelugeSessionViewInstant = false;
bool songLoadGuardActive = false;
uint32_t pendingPostLoadResetSample = 0;
static constexpr uint32_t kPostLoadResetDelaySamples = 2 * kSampleRate;
static constexpr uint32_t kProgrammerKeepaliveSamples = kSampleRate / 2;
static constexpr uint32_t kPeriodicSyncSamples = kSampleRate / 4;
static constexpr uint32_t kCountdownSyncSamples = kSampleRate / 8;
static constexpr uint32_t kMinSyncGapSamples = kSampleRate / 20;

// Scene column in Note view (Deluge row: 7 = top Volume, 0 = bottom Record Arm).
constexpr int32_t kNoteSceneRowMetronome = 7;
constexpr int32_t kNoteSceneRowRedo = 1;
constexpr int32_t kNoteSceneRowUndo = 0;

bool noteSceneUndoLit = false;
bool noteSceneRedoLit = false;
bool launchpadSessionButtonHeld = false;
bool launchpadSessionHoldUsedForPad = false;
bool launchpadInputNeedsSync = false;

bool featureEnabled() {
	return runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::EnableLaunchpadGridMirror);
}

bool launchpadQuiesced() {
	return songLoadGuardActive || loadSongUI.isLoadingSong();
}

void hardResetLaunchpadHardware() {
	MIDICableUSBHosted* port2 = launchpad_cable::getPort2();
	if (port2 == nullptr) {
		return;
	}

	// Programmer mode + LED feedback off + wipe all pad LEDs (safe after corrupt song state).
	launchpad_sysex::sendSetup(port2);
}

bool launchCountdownActive() {
	return playbackHandler.isEitherClockActive() && session.launchEventAtSwungTickCount != 0;
}

void doSync(bool force, bool forceFullRefresh = false) {
	uint32_t now = AudioEngine::audioSampleTimer;
	if (!force && (now - lastSyncSentTime) < kMinSyncGapSamples) {
		return;
	}

	lastSyncSentTime = now;

	if (viewMode == ViewMode::Note) {
		launchpad_note_mode::syncLeds();
	}
	else {
		sessionView.launchpadSyncGridLedsNow(forceFullRefresh);
	}
}

void markLaunchpadInputNeedsSync() {
	launchpadInputNeedsSync = true;
}

void flushLaunchpadInputSync() {
	if (!launchpadInputNeedsSync) {
		return;
	}
	launchpadInputNeedsSync = false;
	doSync(true);
}

bool isPressed(uint8_t statusType, uint8_t data2) {
	if (statusType == 0x09) {
		return data2 > 0;
	}
	if (statusType == 0x0B) {
		return data2 > 0;
	}
	return false;
}

bool tryDelugeSessionViewNow(bool instant) {
	if (sdRoutineLock || currentUIMode != UI_MODE_NONE) {
		return false;
	}

	UI* rootUI = getRootUI();
	if (rootUI == &sessionView || rootUI == &performanceView) {
		return true;
	}

	Clip* clip = getCurrentClip();
	if (clip != nullptr && (currentSong->lastClipInstanceEnteredStartPos != -1 || clip->isArrangementOnlyClip())) {
		if (arrangerView.transitionToArrangementEditor()) {
			return true;
		}
	}

	if (instant) {
		changeRootUI(&sessionView);
		currentUIMode = UI_MODE_NONE;
		getCurrentUI()->focusRegained();
		uiNeedsRendering(getCurrentUI());
		return true;
	}

	sessionView.transitionToSessionView();
	return true;
}

void requestDelugeSessionView(bool instant = false) {
	if (tryDelugeSessionViewNow(instant)) {
		pendingDelugeSessionView = false;
		return;
	}

	pendingDelugeSessionView = true;
	pendingDelugeSessionViewInstant = instant;
}

void tryPendingDelugeSessionView() {
	if (!pendingDelugeSessionView) {
		return;
	}

	if (tryDelugeSessionViewNow(pendingDelugeSessionViewInstant)) {
		pendingDelugeSessionView = false;
	}
}

void reassertLaunchpadLayout() {
	MIDICableUSBHosted* port2 = launchpad_cable::getPort2();
	if (port2 == nullptr) {
		return;
	}

	// Session/Note hardware buttons can knock the Launchpad out of Programmer mode.
	launchpad_sysex::reassertProgrammerMode(port2);
	launchpad_sysex::invalidateLedCache();
}

void clearNoteSceneButtonState() {
	noteSceneUndoLit = false;
	noteSceneRedoLit = false;
}

void resetToSessionMode(MIDICable* cable, int32_t midiChannel, bool fullHardwareReset) {
	if (viewMode == ViewMode::Note && cable != nullptr) {
		launchpad_note_mode::releaseAllHeldPads(*cable, midiChannel);
	}

	launchpad_note_mode::resetState();
	clearNoteSceneButtonState();
	viewMode = ViewMode::Session;

	if (fullHardwareReset) {
		hardResetLaunchpadHardware();
	}
	else {
		reassertLaunchpadLayout();
	}

	if (!launchpadQuiesced()) {
		doSync(true);
	}
}

void buildNoteSceneColours(RGB sceneColours[8]) {
	using deluge::gui::colours::amber;
	using deluge::gui::colours::black;
	using deluge::gui::colours::white;

	// ~10/255 peak — matches inactive session clip dim level after Launchpad scaleRgb.
	static constexpr RGB kSceneHintDim = RGB(10, 4, 0);
	static constexpr RGB kSceneUndoRedoDim = RGB(10, 10, 10);

	for (int32_t y = 0; y < 8; y++) {
		sceneColours[y] = black;
	}

	sceneColours[kNoteSceneRowMetronome] = playbackHandler.metronomeOn ? amber : kSceneHintDim;
	sceneColours[kNoteSceneRowRedo] = noteSceneRedoLit ? white : kSceneUndoRedoDim;
	sceneColours[kNoteSceneRowUndo] = noteSceneUndoLit ? white : kSceneUndoRedoDim;
}

bool handleNoteSceneButton(int32_t sceneRow, bool on) {
	if (sceneRow == kNoteSceneRowMetronome) {
		if (on) {
			playbackHandler.toggleMetronomeStatus();
		}
		return true;
	}

	if (sceneRow == kNoteSceneRowUndo) {
		if (on) {
			if (actionLogger.allowedToDoReversion()) {
				pendingGlobalMIDICommand = GlobalMIDICommand::UNDO;
				pendingGlobalMIDICommandNumClustersWritten = 0;
				playbackHandler.slowRoutine();
			}
			noteSceneUndoLit = true;
		}
		else {
			noteSceneUndoLit = false;
		}
		return true;
	}

	if (sceneRow == kNoteSceneRowRedo) {
		if (on) {
			if (actionLogger.allowedToDoReversion()) {
				pendingGlobalMIDICommand = GlobalMIDICommand::REDO;
				pendingGlobalMIDICommandNumClustersWritten = 0;
				playbackHandler.slowRoutine();
			}
			noteSceneRedoLit = true;
		}
		else {
			noteSceneRedoLit = false;
		}
		return true;
	}

	return false;
}

void switchViewMode(ViewMode newMode, MIDICable& cable, int32_t midiChannel) {
	if (viewMode == newMode) {
		return;
	}

	if (viewMode == ViewMode::Note) {
		launchpad_note_mode::releaseAllHeldPads(cable, midiChannel);
		clearNoteSceneButtonState();
	}

	viewMode = newMode;
	reassertLaunchpadLayout();
	doSync(true);
}

bool handleSessionButton(MIDICable& cable, bool on, int32_t midiChannel) {
	if (on) {
		launchpadSessionButtonHeld = true;
		launchpadSessionHoldUsedForPad = false;
		return false;
	}

	launchpadSessionButtonHeld = false;
	if (launchpadSessionHoldUsedForPad) {
		launchpadSessionHoldUsedForPad = false;
		return false;
	}

	if (viewMode == ViewMode::Session) {
		requestDelugeSessionView(false);
		reassertLaunchpadLayout();
	}
	else {
		switchViewMode(ViewMode::Session, cable, midiChannel);
	}
	return true;
}

bool handleControlChange(MIDICable& cable, uint8_t cc, bool on, int32_t midiChannel) {
	if (cc == launchpad_programmer_map::cc::kSession) {
		return handleSessionButton(cable, on, midiChannel);
	}

	if (viewMode == ViewMode::Note) {
		int32_t sceneRow = 0;
		if (launchpad_programmer_map::sceneRowFromCc(cc, sceneRow)) {
			if (handleNoteSceneButton(sceneRow, on)) {
				return true;
			}
		}
	}

	if (!on) {
		return false;
	}

	switch (cc) {
	case launchpad_programmer_map::cc::kNote:
		if (viewMode == ViewMode::Note) {
			if (launchpad_note_mode::toggleExpressiveChordsSubMode(cable, midiChannel)) {
				reassertLaunchpadLayout();
				return true;
			}
			reassertLaunchpadLayout();
			return true;
		}
		if (!launchpad_note_mode::canEnterNoteMode()) {
			return true;
		}
		switchViewMode(ViewMode::Note, cable, midiChannel);
		return true;
	default:
		break;
	}

	if (viewMode == ViewMode::Session) {
		int32_t sceneRow;
		if (launchpad_programmer_map::sceneRowFromCc(cc, sceneRow)) {
			sessionView.launchpadStartSectionFromRow(sceneRow);
			return true;
		}
	}

	switch (cc) {
	case launchpad_programmer_map::cc::kUp:
		if (viewMode == ViewMode::Note) {
			return launchpad_note_mode::handleScroll(0, -1);
		}
		sessionView.launchpadGridScroll(0, -1);
		return true;
	case launchpad_programmer_map::cc::kDown:
		if (viewMode == ViewMode::Note) {
			return launchpad_note_mode::handleScroll(0, 1);
		}
		sessionView.launchpadGridScroll(0, 1);
		return true;
	case launchpad_programmer_map::cc::kLeft:
		if (viewMode == ViewMode::Note) {
			return launchpad_note_mode::handleScroll(-1, 0);
		}
		sessionView.launchpadGridScroll(-1, 0);
		return true;
	case launchpad_programmer_map::cc::kRight:
		if (viewMode == ViewMode::Note) {
			return launchpad_note_mode::handleScroll(1, 0);
		}
		sessionView.launchpadGridScroll(1, 0);
		return true;
	case launchpad_programmer_map::cc::kCustom:
		sessionView.launchpadTogglePlayStop();
		return true;
	case launchpad_programmer_map::cc::kCaptureMidi:
		sessionView.launchpadToggleRecord();
		return true;
	default:
		return true;
	}
}

bool handleNote(MIDICable& cable, uint8_t note, uint8_t velocity, bool on, int32_t midiChannel) {
	int32_t x;
	int32_t y;
	if (!launchpad_programmer_map::delugeGridCoordsFromNote(note, x, y)) {
		return false;
	}

	if (viewMode == ViewMode::Note) {
		return launchpad_note_mode::handlePad(cable, x, y, velocity, on, midiChannel);
	}

	sessionView.gridHandlePadsFromLaunchpad(x, y, on ? 1 : 0);
	return true;
}

void sendLeds(RGB image[][kDisplayWidth + kSideBarWidth], uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
              RGB const sceneColours[8], bool forceFullRefresh = false) {
	MIDICableUSBHosted* cable = launchpad_cable::getPort2();
	if (cable == nullptr) {
		return;
	}

	bool transportPlaying = playbackHandler.playbackState != 0;
	bool transportRecording = playbackHandler.recording == RecordingMode::NORMAL;
	launchpad_sysex::sendLaunchpadLeds(cable, viewMode, image, occupancyMask, sceneColours, transportPlaying,
	                                   transportRecording, forceFullRefresh);
	midiEngine.flushMIDI();
}

} // namespace

bool isSessionButtonHeld() {
	return launchpadSessionButtonHeld;
}

void notifySessionHoldUsedForPad() {
	launchpadSessionHoldUsedForPad = true;
}

ViewMode getViewMode() {
	return viewMode;
}

bool isLaunchCountdownActive() {
	return launchCountdownActive();
}

void requestSync() {
	if (!featureEnabled() || launchpadQuiesced()) {
		return;
	}

	if (launchpad_cable::getPort2() == nullptr) {
		return;
	}

	doSync(true);
}

void forceSessionDefault(bool fullHardwareReset) {
	if (!featureEnabled()) {
		return;
	}

	resetToSessionMode(launchpad_cable::getPort2(), 0, fullHardwareReset);
	requestDelugeSessionView(true);
	tryPendingDelugeSessionView();
}

void onDeviceConnected() {
	ignoreLaunchpadInputUntilSample = AudioEngine::audioSampleTimer + (3 * kSampleRate);
	lastProgrammerKeepaliveTime = 0;
	forceSessionDefault(true);
}

void onSongLoadStarting() {
	if (!featureEnabled()) {
		return;
	}

	songLoadGuardActive = true;
	ignoreLaunchpadInputUntilSample = AudioEngine::audioSampleTimer + (4 * kSampleRate);
	pendingDelugeSessionView = true;
	pendingDelugeSessionViewInstant = true;

	launchpad_note_mode::resetState();
	clearNoteSceneButtonState();
	launchpadSessionButtonHeld = false;
	launchpadSessionHoldUsedForPad = false;
	viewMode = ViewMode::Session;
	hardResetLaunchpadHardware();
}

void onSongLoadAborted() {
	songLoadGuardActive = false;

	if (!featureEnabled() || currentSong == nullptr) {
		return;
	}

	forceSessionDefault(true);
}

void onSongSwappedDuringLoad() {
	if (!featureEnabled() || !songLoadGuardActive) {
		return;
	}

	hardResetLaunchpadHardware();
}

void onSongLoaded() {
	if (!featureEnabled()) {
		songLoadGuardActive = false;
		return;
	}

	sessionView.launchpadResetMirrorState();
	launchpad_note_mode::resetState();
	clearNoteSceneButtonState();
	viewMode = ViewMode::Session;
	ignoreLaunchpadInputUntilSample = AudioEngine::audioSampleTimer + (3 * kSampleRate);
	lastProgrammerKeepaliveTime = 0;
	pendingDelugeSessionView = true;
	pendingDelugeSessionViewInstant = true;

	// Swap may have sent PGMs while guard was up; wipe hardware once load I/O is finished.
	hardResetLaunchpadHardware();
	requestDelugeSessionView(true);
	tryPendingDelugeSessionView();
	songLoadGuardActive = false;

	// Full refresh — partial SysEx wipes during SD load can leave stale pads if we only send deltas.
	doSync(true, true);
	pendingPostLoadResetSample = AudioEngine::audioSampleTimer + kPostLoadResetDelaySamples;
}

void periodicSyncIfNeeded() {
	if (!featureEnabled()) {
		return;
	}

	if (launchpad_cable::getPort2() == nullptr) {
		return;
	}

	uint32_t now = AudioEngine::audioSampleTimer;

	if (launchpadQuiesced()) {
		// Long SD loads skip grid sync but must keep Programmer mode (Live mode + routed MIDI = garbage LEDs).
		if (now - lastProgrammerKeepaliveTime >= kProgrammerKeepaliveSamples) {
			lastProgrammerKeepaliveTime = now;
			MIDICableUSBHosted* port2 = launchpad_cable::getPort2();
			if (port2 != nullptr) {
				launchpad_sysex::reassertProgrammerMode(port2);
			}
		}
		tryPendingDelugeSessionView();
		return;
	}

	if (pendingPostLoadResetSample != 0 && now >= pendingPostLoadResetSample) {
		pendingPostLoadResetSample = 0;
		hardResetLaunchpadHardware();
		doSync(true, true);
	}

	tryPendingDelugeSessionView();

	if (viewMode == ViewMode::Note) {
		launchpad_note_mode::serviceRepeats();
	}
	else {
		if (now - lastProgrammerKeepaliveTime >= kProgrammerKeepaliveSamples) {
			lastProgrammerKeepaliveTime = now;
			MIDICableUSBHosted* port2 = launchpad_cable::getPort2();
			if (port2 != nullptr) {
				launchpad_sysex::reassertProgrammerMode(port2);
			}
		}
	}

	now = AudioEngine::audioSampleTimer;
	uint32_t interval = kPeriodicSyncSamples;
	if (launchCountdownActive() || sessionView.launchpadMirrorWantsFastSync()) {
		interval = kCountdownSyncSamples;
	}
	if (now - lastPeriodicSyncTime < interval) {
		return;
	}

	lastPeriodicSyncTime = now;
	doSync(false);
}

void syncSessionGrid(RGB image[][kDisplayWidth + kSideBarWidth], uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                     RGB const sceneColours[8], bool forceFullRefresh) {
	sendLeds(image, occupancyMask, sceneColours, forceFullRefresh);
}

void syncNoteView(RGB image[][kDisplayWidth + kSideBarWidth], uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	RGB sceneColours[8];
	buildNoteSceneColours(sceneColours);
	sendLeds(image, occupancyMask, sceneColours);
}

bool handleMidiMessage(MIDICable& cable, uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2) {
	if (!featureEnabled()) {
		return false;
	}

	if (!launchpad_cable::isPort2(cable)) {
		return false;
	}

	if (currentSong == nullptr || launchpadQuiesced()) {
		return true;
	}

	if (AudioEngine::audioSampleTimer < ignoreLaunchpadInputUntilSample) {
		return true;
	}

	launchpadInputNeedsSync = false;

	bool on = isPressed(statusType, data2);
	bool handled = false;
	bool modeChanged = false;

	if (statusType == 0x0B && data1 < 120) {
		if (on && (data1 == launchpad_programmer_map::cc::kSession || data1 == launchpad_programmer_map::cc::kNote)) {
			ViewMode previousMode = viewMode;
			handled = handleControlChange(cable, data1, on, channel);
			modeChanged = viewMode != previousMode;
		}
		else {
			handled = handleControlChange(cable, data1, on, channel);
		}
	}

	if (statusType == 0x09 || statusType == 0x08) {
		uint8_t velocity = data2;
		if (statusType == 0x08) {
			velocity = 0;
		}
		handled = handleNote(cable, data1, velocity, on, channel) || handled;
	}

	if (viewMode == ViewMode::Note) {
		if (statusType == 0x0A) {
			int32_t x = 0;
			int32_t y = 0;
			if (launchpad_programmer_map::delugeGridCoordsFromNote(data1, x, y)) {
				handled = launchpad_note_mode::handlePadPressure(x, y, data2) || handled;
			}
		}
		else if (statusType == 0x0D) {
			handled = launchpad_note_mode::handleChannelPressure(data1) || handled;
		}
	}

	if (handled && !modeChanged) {
		markLaunchpadInputNeedsSync();
	}
	flushLaunchpadInputSync();

	return true;
}

bool shouldBlockOutgoingMidi(MIDICableUSB& cable, MIDIMessage message) {
	if (!featureEnabled()) {
		return false;
	}

	// Block port 1 (DAW) too — song MIDI routed there lights pads in Live mode.
	if (!launchpad_cable::isLaunchpadCable(cable)) {
		return false;
	}

	if (launchpadQuiesced()) {
		return message.statusType >= 0x08 && message.statusType <= 0x0E;
	}

	if (message.statusType >= 0x08 && message.statusType <= 0x0E) {
		return true;
	}

	return false;
}

} // namespace launchpad_extension
