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
#include "io/midi/midi_follow.h"

using namespace deluge;
using namespace gui;

namespace {

using deluge::controls::TakeoverMode;
static_assert(static_cast<TakeoverMode>(MIDITakeoverMode::JUMP) == TakeoverMode::JUMP);
static_assert(static_cast<TakeoverMode>(MIDITakeoverMode::PICKUP) == TakeoverMode::PICKUP);
static_assert(static_cast<TakeoverMode>(MIDITakeoverMode::SCALE) == TakeoverMode::SCALE);
static_assert(static_cast<TakeoverMode>(MIDITakeoverMode::RELATIVE) == TakeoverMode::RELATIVE);

/// The controller's remembered position, from where this message's controller keeps it: the learned knob, or the MIDI
/// follow table's slot for the CC; none for a message with neither.
controls::TakeoverState stateOf(const MIDIKnob* knob, bool doingMidiFollow, int32_t ccNumber) {
	if (knob != nullptr) {
		if (knob->previousPositionSaved) {
			return {knob->previousPosition};
		}
	}
	else if (doingMidiFollow && midiFollow.previousKnobPos[ccNumber] != kNoSelection) {
		return {midiFollow.previousKnobPos[ccNumber]};
	}
	return {};
}

/// Keeps the takeover's new remembered position where stateOf found it.
void keep(const controls::TakeoverState& state, MIDIKnob* knob, bool doingMidiFollow, int32_t ccNumber) {
	if (!state.previous) {
		return;
	}
	if (knob != nullptr) {
		knob->previousPosition = *state.previous;
		knob->previousPositionSaved = true;
	}
	else if (doingMidiFollow) {
		midiFollow.previousKnobPos[ccNumber] = *state.previous;
	}
}

} // namespace

/// based on the midi takeover default setting of RELATIVE, JUMP, PICKUP, or SCALE
/// this function will calculate the knob position that the deluge parameter that the midi cc
/// received is learned to should be set at based on the midi cc value received
int32_t MidiTakeover::calculateKnobPos(MIDICable& cable, int32_t knobPos, int32_t ccValue, MIDIKnob* knob,
                                       bool doingMidiFollow, int32_t ccNumber, bool isStepEditing) {
	const controls::TakeoverInputs inputs{
	    .mode = static_cast<TakeoverMode>(midiEngine.midiTakeover),
	    .cable_relative = cable.is_relative,
	    .knob_relative = knob != nullptr && knob->relative,
	    .recording = playbackHandler.isEitherClockActive() && (playbackHandler.recording != RecordingMode::OFF),
	    .step_editing = isStepEditing,
	};
	controls::TakeoverState state = stateOf(knob, doingMidiFollow, ccNumber);
	const int32_t newKnobPos = controls::knob_pos_after(knobPos, static_cast<uint8_t>(ccValue), inputs, state);
	keep(state, knob, doingMidiFollow, ccNumber);
	return newKnobPos;
}
