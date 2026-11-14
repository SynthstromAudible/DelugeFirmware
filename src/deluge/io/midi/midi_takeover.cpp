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

#include "io/midi/midi_takeover.h"
#include "controls/takeover.h"
#include "definitions_cxx.hpp"
#include "gui/colour/colour.h" // namespace gui (was reached transitively via util/functions.h)
#include "hid/display/display.h"
#include "io/midi/midi_engine.h"

using namespace deluge;
using namespace gui;

namespace {

using deluge::controls::TakeoverMode;
static_assert(static_cast<TakeoverMode>(MIDITakeoverMode::JUMP) == TakeoverMode::JUMP);
static_assert(static_cast<TakeoverMode>(MIDITakeoverMode::PICKUP) == TakeoverMode::PICKUP);
static_assert(static_cast<TakeoverMode>(MIDITakeoverMode::SCALE) == TakeoverMode::SCALE);
static_assert(static_cast<TakeoverMode>(MIDITakeoverMode::RELATIVE) == TakeoverMode::RELATIVE);

/// The controller's remembered position, from the learned knob: none until it has been sent one.
controls::TakeoverState stateOf(const MIDIKnob& knob) {
	return knob.previousPositionSaved ? controls::TakeoverState{knob.previousPosition} : controls::TakeoverState{};
}

/// Keeps the takeover's new remembered position on the knob.
void keep(const controls::TakeoverState& state, MIDIKnob& knob) {
	if (!state.previous) {
		return;
	}
	knob.previousPosition = *state.previous;
	knob.previousPositionSaved = true;
}

} // namespace

/// based on the midi takeover default setting of RELATIVE, JUMP, PICKUP, or SCALE
/// this function will calculate the knob position that the deluge parameter that the midi cc
/// received is learned to should be set at based on the midi cc value received
int32_t MidiTakeover::calculateKnobPos(MIDICable& cable, int32_t knobPos, int32_t ccValue, MIDIKnob& knob,
                                       bool isStepEditing) {
	const controls::TakeoverInputs inputs{
	    .mode = static_cast<TakeoverMode>(midiEngine.midiTakeover),
	    .cable_relative = cable.is_relative,
	    .knob_relative = knob.relative,
	    .recording = playbackHandler.isEitherClockActive() && (playbackHandler.recording != RecordingMode::OFF),
	    .step_editing = isStepEditing,
	};
	controls::TakeoverState state = stateOf(knob);
	const int32_t newKnobPos = controls::knob_pos_after(knobPos, static_cast<uint8_t>(ccValue), inputs, state);
	keep(state, knob);
	return newKnobPos;
}
