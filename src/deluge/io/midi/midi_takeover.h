/*
 * Copyright (c) 2023 Sean Ditny
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
#include "modulation/knob.h"
#include "modulation/params/param_set.h"

namespace MidiTakeover {
int32_t calculateKnobPos(int32_t knobPos, int32_t ccValue, MIDIKnob* knob = nullptr, bool doingMidiFollow = false,
                         int32_t ccNumber = MIDI_CC_NONE, bool isStepEditing = false);
} // namespace MidiTakeover
