/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#include "io/midi/device_specific/launchpad_extension.h"

#include "definitions_cxx.hpp"
#include "extern.h"
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
#include "model/midi/message.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"

namespace launchpad_extension {

namespace {

ViewMode viewMode = ViewMode::Session;

uint32_t lastPeriodicSyncTime = 0;
uint32_t lastSyncSentTime = 0;
uint32_t lastProgrammerKeepaliveTime = 0;
uint32_t ignoreModeButtonCcUntilSample = 0;
static constexpr uint32_t kProgrammerKeepaliveSamples = kSampleRate / 2;
static constexpr uint32_t kPeriodicSyncSamples = kSampleRate / 4;
static constexpr uint32_t kCountdownSyncSamples = kSampleRate / 8;
static constexpr uint32_t kMinSyncGapSamples = kSampleRate / 20;

bool featureEnabled() {
	return runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::EnableLaunchpadGridMirror);
}

bool launchCountdownActive() {
	return playbackHandler.isEitherClockActive() && session.launchEventAtSwungTickCount != 0;
}

void doSync(bool force) {
	uint32_t now = AudioEngine::audioSampleTimer;
	if (!force && (now - lastSyncSentTime) < kMinSyncGapSamples) {
		return;
	}

	lastSyncSentTime = now;

	if (viewMode == ViewMode::Note) {
		launchpad_note_mode::syncLeds();
	}
	else {
		sessionView.launchpadSyncGridLedsNow();
	}
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

void requestDelugeSessionView() {
	if (sdRoutineLock || currentUIMode != UI_MODE_NONE || getCurrentClip() == nullptr) {
		return;
	}

	UI* rootUI = getRootUI();
	if (rootUI == &sessionView || rootUI == &performanceView) {
		return;
	}

	if (currentSong->lastClipInstanceEnteredStartPos != -1 || getCurrentClip()->isArrangementOnlyClip()) {
		if (arrangerView.transitionToArrangementEditor()) {
			return;
		}
	}

	sessionView.transitionToSessionView();
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

void resetToSessionMode(MIDICable* cable, int32_t midiChannel) {
	if (viewMode == ViewMode::Note && cable != nullptr) {
		launchpad_note_mode::releaseAllHeldPads(*cable, midiChannel);
	}

	launchpad_note_mode::resetState();
	viewMode = ViewMode::Session;
	reassertLaunchpadLayout();
	doSync(true);
}

void switchViewMode(ViewMode newMode, MIDICable& cable, int32_t midiChannel) {
	if (viewMode == newMode) {
		return;
	}

	if (viewMode == ViewMode::Note) {
		launchpad_note_mode::releaseAllHeldPads(cable, midiChannel);
	}

	viewMode = newMode;
	reassertLaunchpadLayout();
	doSync(true);
}

bool handleControlChange(MIDICable& cable, uint8_t cc, bool on, int32_t midiChannel) {
	if (!on) {
		return true;
	}

	switch (cc) {
	case launchpad_programmer_map::cc::kSession:
		if (AudioEngine::audioSampleTimer < ignoreModeButtonCcUntilSample) {
			return true;
		}
		if (viewMode == ViewMode::Session) {
			requestDelugeSessionView();
			reassertLaunchpadLayout();
			doSync(true);
			return true;
		}
		switchViewMode(ViewMode::Session, cable, midiChannel);
		return true;
	case launchpad_programmer_map::cc::kNote:
		if (AudioEngine::audioSampleTimer < ignoreModeButtonCcUntilSample) {
			return true;
		}
		if (viewMode == ViewMode::Note) {
			if (launchpad_note_mode::toggleExpressiveChordsSubMode(cable, midiChannel)) {
				reassertLaunchpadLayout();
				doSync(true);
				return true;
			}
			reassertLaunchpadLayout();
			doSync(true);
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
		return true;
	}

	if (viewMode == ViewMode::Note) {
		return launchpad_note_mode::handlePad(cable, x, y, velocity, on, midiChannel);
	}

	sessionView.gridHandlePadsFromLaunchpad(x, y, on ? 1 : 0);
	return true;
}

void sendLeds(RGB image[][kDisplayWidth + kSideBarWidth], uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
              RGB const sceneColours[8]) {
	MIDICableUSBHosted* cable = launchpad_cable::getPort2();
	if (cable == nullptr) {
		return;
	}

	bool transportPlaying = playbackHandler.playbackState != 0;
	bool transportRecording = playbackHandler.recording == RecordingMode::NORMAL;
	launchpad_sysex::sendLaunchpadLeds(cable, viewMode, image, occupancyMask, sceneColours, transportPlaying,
	                                   transportRecording);
	midiEngine.flushMIDI();
}

} // namespace

ViewMode getViewMode() {
	return viewMode;
}

void requestSync() {
	if (!featureEnabled()) {
		return;
	}

	if (launchpad_cable::getPort2() == nullptr) {
		return;
	}

	doSync(true);
}

void forceSessionDefault() {
	if (!featureEnabled()) {
		return;
	}

	resetToSessionMode(launchpad_cable::getPort2(), 0);
	requestDelugeSessionView();
}

void onDeviceConnected() {
	ignoreModeButtonCcUntilSample = AudioEngine::audioSampleTimer + (3 * kSampleRate);
	lastProgrammerKeepaliveTime = 0;
	forceSessionDefault();
}

void onSongLoaded() {
	ignoreModeButtonCcUntilSample = AudioEngine::audioSampleTimer + (2 * kSampleRate);
	lastProgrammerKeepaliveTime = 0;
	forceSessionDefault();
}

void periodicSyncIfNeeded() {
	if (!featureEnabled()) {
		return;
	}

	if (launchpad_cable::getPort2() == nullptr) {
		return;
	}

	if (viewMode == ViewMode::Note) {
		launchpad_note_mode::serviceRepeats();
	}
	else {
		uint32_t now = AudioEngine::audioSampleTimer;
		if (now - lastProgrammerKeepaliveTime >= kProgrammerKeepaliveSamples) {
			lastProgrammerKeepaliveTime = now;
			MIDICableUSBHosted* port2 = launchpad_cable::getPort2();
			if (port2 != nullptr) {
				launchpad_sysex::reassertProgrammerMode(port2);
			}
		}
	}

	uint32_t now = AudioEngine::audioSampleTimer;
	uint32_t interval = launchCountdownActive() ? kCountdownSyncSamples : kPeriodicSyncSamples;
	if (now - lastPeriodicSyncTime < interval) {
		return;
	}

	lastPeriodicSyncTime = now;
	doSync(false);
}

void syncSessionGrid(RGB image[][kDisplayWidth + kSideBarWidth], uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                     RGB const sceneColours[8]) {
	sendLeds(image, occupancyMask, sceneColours);
}

void syncNoteView(RGB image[][kDisplayWidth + kSideBarWidth], uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	static RGB const kBlackScenes[8] = {};
	sendLeds(image, occupancyMask, kBlackScenes);
}

bool handleMidiMessage(MIDICable& cable, uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2) {
	if (!featureEnabled()) {
		return false;
	}

	if (!launchpad_cable::isPort2(cable)) {
		return false;
	}

	if (currentSong == nullptr) {
		return true;
	}

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
		requestSync();
	}

	return true;
}

bool shouldBlockOutgoingMidi(MIDICableUSB& cable, MIDIMessage message) {
	if (!featureEnabled()) {
		return false;
	}

	if (!launchpad_cable::isPort2(cable)) {
		return false;
	}

	if (message.statusType >= 0x08 && message.statusType <= 0x0E) {
		return true;
	}

	return false;
}

} // namespace launchpad_extension
