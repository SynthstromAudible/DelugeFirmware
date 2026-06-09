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

void syncSessionGrid(RGB image[][kDisplayWidth + kSideBarWidth], uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                     RGB const sceneColours[8]);

void syncNoteView(RGB image[][kDisplayWidth + kSideBarWidth], uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);

// ~4 Hz background refresh (GRAPHICS_ROUTINE). No-op when feature off or no Launchpad connected.
void periodicSyncIfNeeded();

// Immediate refresh after grid/transport interaction.
void requestSync();

void onDeviceConnected();

bool handleMidiMessage(MIDICable& cable, uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2);

// Programmer mode lights pads on any incoming Note/CC — block all channel MIDI out to port 2.
bool shouldBlockOutgoingMidi(MIDICableUSB& cable, MIDIMessage message);

} // namespace launchpad_extension
