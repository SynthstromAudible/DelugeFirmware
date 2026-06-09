/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#include "io/midi/device_specific/launchpad_extension.h"

#include "definitions_cxx.hpp"
#include "gui/views/session_view.h"
#include "io/midi/cable_types/usb_common.h"
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/device_specific/launchpad_cable.h"
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

uint32_t lastPeriodicSyncTime = 0;
uint32_t lastSyncSentTime = 0;
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
	sessionView.launchpadSyncGridLedsNow();
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

bool handleControlChange(uint8_t cc, bool on) {
	if (!on) {
		return true;
	}

	int32_t sceneRow;
	if (launchpad_programmer_map::sceneRowFromCc(cc, sceneRow)) {
		sessionView.launchpadStartSectionFromRow(sceneRow);
		return true;
	}

	switch (cc) {
	case launchpad_programmer_map::cc::kUp:
		sessionView.launchpadGridScroll(0, -1);
		return true;
	case launchpad_programmer_map::cc::kDown:
		sessionView.launchpadGridScroll(0, 1);
		return true;
	case launchpad_programmer_map::cc::kLeft:
		sessionView.launchpadGridScroll(-1, 0);
		return true;
	case launchpad_programmer_map::cc::kRight:
		sessionView.launchpadGridScroll(1, 0);
		return true;
	case launchpad_programmer_map::cc::kCustom:
		sessionView.launchpadTogglePlayStop();
		return true;
	case launchpad_programmer_map::cc::kCaptureMidi:
		sessionView.launchpadToggleRecord();
		return true;
	case launchpad_programmer_map::cc::kSession:
	case launchpad_programmer_map::cc::kNote:
	default:
		return true;
	}
}

bool handleNote(uint8_t note, bool on) {
	int32_t x;
	int32_t y;
	if (!launchpad_programmer_map::delugeGridCoordsFromNote(note, x, y)) {
		return true;
	}

	sessionView.gridHandlePadsFromLaunchpad(x, y, on);
	return true;
}

} // namespace

void requestSync() {
	if (!featureEnabled()) {
		return;
	}

	if (launchpad_cable::getPort2() == nullptr) {
		return;
	}

	doSync(true);
}

void onDeviceConnected() {
	doSync(true);
}

void periodicSyncIfNeeded() {
	if (!featureEnabled()) {
		return;
	}

	if (launchpad_cable::getPort2() == nullptr) {
		return;
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
	MIDICableUSBHosted* cable = launchpad_cable::getPort2();
	if (cable == nullptr) {
		return;
	}

	bool transportPlaying = playbackHandler.playbackState != 0;
	bool transportRecording = playbackHandler.recording == RecordingMode::NORMAL;
	launchpad_sysex::sendSessionGrid(cable, image, occupancyMask, sceneColours, transportPlaying, transportRecording);
	midiEngine.flushMIDI();
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
	bool gridAction = false;

	if (statusType == 0x0B && data1 < 120) {
		gridAction = handleControlChange(data1, on);
	}

	if (statusType == 0x09 || statusType == 0x08) {
		gridAction = handleNote(data1, on) || gridAction;
	}

	if (gridAction) {
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

	// SysEx uses sendSysex(), not sendMessage(). Block channel voice traffic — it lights pads.
	if (message.statusType >= 0x08 && message.statusType <= 0x0E) {
		return true;
	}

	return false;
}

} // namespace launchpad_extension
