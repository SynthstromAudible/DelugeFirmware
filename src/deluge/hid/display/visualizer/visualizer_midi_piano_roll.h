/*
 * Copyright (c) 2025 Bruce Zawadzki (Tone Coder)
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

#include "hid/display/oled_canvas/canvas.h"

namespace deluge::hid::display {

/// Render MIDI piano roll visualizer on OLED display
/// Shows MIDI notes (both input and output) as downward-scrolling piano roll
void renderVisualizerMidiPianoRoll(oled_canvas::Canvas& canvas);

/// Hook for MIDI note events - called from midiEngine.sendNote() and input processing
/// @param note MIDI note number (0-127)
/// @param on true for note-on, false for note-off
/// @param velocity Note velocity
/// @param visualizerActive true if MIDI piano roll visualizer is currently active
/// @param isInput true if this is input MIDI, false if output MIDI (default: false)
void midiPianoRollNoteEvent(uint8_t note, bool on, uint8_t velocity, bool visualizerActive, bool isInput = false);

} // namespace deluge::hid::display
