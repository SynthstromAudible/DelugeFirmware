/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "gui/colour/rgb.h"

class InstrumentClip;
class MIDICable;

namespace launchpad_note_mode {

// False for audio clips and other non-note-editable tracks.
bool canEnterNoteMode();

void syncLeds();

bool toggleExpressiveChordsSubMode(MIDICable& cable, int32_t midiChannel);

bool handlePad(MIDICable& cable, int32_t x, int32_t y, uint8_t velocity, bool on, int32_t midiChannel);

bool handlePadPressure(int32_t x, int32_t y, uint8_t pressure);

bool handleChannelPressure(uint8_t pressure);

bool handleScroll(int32_t offsetX, int32_t offsetY);

void serviceRepeats();

void resetState();

void releaseAllHeldPads(MIDICable& cable, int32_t midiChannel);

} // namespace launchpad_note_mode
