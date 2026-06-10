/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "gui/colour/rgb.h"

class MIDICable;
class MIDICableUSB;
struct MIDIMessage;

namespace launchpad_extension {

enum class ViewMode : uint8_t {
	Session,
	Note,
};

ViewMode getViewMode();

// Session hardware button held = Deluge shift for grid pads (immediate clip on/off).
bool isSessionButtonHeld();
void notifySessionHoldUsedForPad();

void syncSessionGrid(RGB image[][kDisplayWidth + kSideBarWidth], uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                     RGB const sceneColours[8], bool forceFullRefresh = false);

void syncNoteView(RGB image[][kDisplayWidth + kSideBarWidth], uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);

// Launchpad LED refresh policy:
// - periodicSyncIfNeeded(): ~4 Hz (8 Hz during launch countdown / armed-clip pulse). Rate-limited deltas.
// - requestSync(): immediate refresh — call from Deluge UI navigation (changeRootUI) and Deluge hardware
//   grid/scroll input only. Do not call from Launchpad MIDI handlers; handleMidiMessage coalesces one sync
//   per handled message at the tail.
// - Deluge view changes always go through changeRootUI / changeUISideways, which call requestSync().
void periodicSyncIfNeeded();

// Immediate mirror refresh (bypasses periodic min-gap). Deluge-side changes only — see policy above.
void requestSync();

// True while session launch countdown is active (Launchpad progress ticker).
bool isLaunchCountdownActive();

void onDeviceConnected();

// Freeze and clear Launchpad before reading a song from SD (blocks MIDI in/out until load finishes).
void onSongLoadStarting();

void onSongLoadAborted();

// Called from doSongSwap while a file load is in progress (after PGM burst).
void onSongSwappedDuringLoad();

void onSongLoaded();

bool handleMidiMessage(MIDICable& cable, uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2);

// Programmer mode lights pads on any incoming Note/CC — block all channel MIDI out to port 2.
bool shouldBlockOutgoingMidi(MIDICableUSB& cable, MIDIMessage message);

} // namespace launchpad_extension
