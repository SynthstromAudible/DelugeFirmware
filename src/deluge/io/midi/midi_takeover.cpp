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
#include "definitions_cxx.hpp"
#include "hid/display/display.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"

extern "C" {
#include "RZA1/uart/sio_char.h"
}

using namespace deluge;
using namespace gui;

void savePreviousKnobPos(int32_t knobPos, MIDIKnob* knob = nullptr, bool doingMidiFollow = false,
                         int32_t ccNumber = MIDI_CC_NONE);
void saveKnobPos(int32_t knobPos, MIDIKnob* knob);
void saveKnobPos(int32_t knobPos, int32_t ccNumber);
int32_t getPreviousKnobPos(int32_t knobPos, MIDIKnob* knob = nullptr, bool doingMidiFollow = false,
                           int32_t ccNumber = MIDI_CC_NONE);

/// based on the midi takeover default setting of RELATIVE, JUMP, PICKUP, or SCALE
/// this function will calculate the knob position that the deluge parameter that the midi cc
/// received is learned to should be set at based on the midi cc value received
int32_t MidiTakeover::calculateKnobPos(int32_t knobPos, int32_t ccValue, MIDIKnob* knob, bool doingMidiFollow,
                                       int32_t ccNumber, bool isStepEditing) {
	/*

	Step #1: Convert Midi Controller's CC Value to Deluge Knob Position Value

	- Midi CC Values for non endless encoders typically go from 0 to 127
	- Deluge Knob Position Value goes from -64 to 64

	To convert Midi CC Value to Deluge Knob Position Value, subtract 64 from the Midi CC Value

	So a Midi CC Value of 0 is equal to a Deluge Knob Position Value of -64 (0 less 64).

	Similarly a Midi CC Value of 127 is equal to a Deluge Knob Position Value of +63 (127 less 64)

	*/

	int32_t midiKnobPos = 64;
	if (ccValue < kMaxMIDIValue) {
		midiKnobPos = ccValue - 64;
	}

	// we'll first set newKnobPos equal to current knobPos
	// if newKnobPos should be updated it will happen below
	int32_t newKnobPos = knobPos;

	bool isRecording = playbackHandler.isEitherClockActive() && (playbackHandler.recording != RecordingMode::OFF);

	// for controller's sending relative values
	if (((knob != nullptr) && (knob->relative)) || (midiEngine.midiTakeover == MIDITakeoverMode::RELATIVE)) {
		int32_t offset = ccValue;
		if (offset >= 64) {
			offset -= 128;
		}
		int32_t lowerLimit = std::min(-64_i32, knobPos);
		newKnobPos = knobPos + offset;
		newKnobPos = std::max(newKnobPos, lowerLimit);
		newKnobPos = std::min(newKnobPos, 64_i32);

		savePreviousKnobPos(newKnobPos, knob, doingMidiFollow, ccNumber);
	}
	// deluge value will always jump to the current midi controller value
	else if (midiEngine.midiTakeover == MIDITakeoverMode::JUMP || isRecording || isStepEditing) {
		newKnobPos = midiKnobPos;

		savePreviousKnobPos(newKnobPos, knob, doingMidiFollow, ccNumber);
	}
	else { // Midi Takeover Mode = Pickup or Value Scaling
		// Get or Save previous knob position for first time
		// The first time a midi knob is turned in a session, no previous midi knob position information exists, so to
		// start, it will be equal to the current midiKnobPos.

		int32_t previousKnobPosition = getPreviousKnobPos(midiKnobPos, knob, doingMidiFollow, ccNumber);

		// have we met or exceeded the deluge knob position in either direction?
		// if so, we've "picked up"
		// note: if previous knob position information becomes invalid (e.g. switching banks / unplugging midi
		// controller) then the deluge will behave like "jump" and jump to the current midi controller value
		if (((previousKnobPosition <= knobPos) && (midiKnobPos >= knobPos))
		    || ((previousKnobPosition >= knobPos) && (midiKnobPos <= knobPos))) {
			newKnobPos = midiKnobPos;
		}
		// if we haven't picked up and scale is enabled, we'll scale deluge value in direction knob is turning
		// so that the deluge knob position and midi knob position reach the min/max of knob range at same time
		else if (midiEngine.midiTakeover == MIDITakeoverMode::SCALE) {
			// Set the max and min of the deluge midi knob position range
			int32_t knobMaxPos = 64;
			int32_t knobMinPos = -64;

			// position range
			int32_t delugeKnobMaxPosDelta = knobMaxPos - knobPos; // Positive Runway
			int32_t delugeKnobMinPosDelta = knobPos - knobMinPos; // Negative Runway

			// calculate amount of midi "knob runway" is remaining from current knob position to max and min of knob
			// position range
			int32_t midiKnobMaxPosDelta = knobMaxPos - midiKnobPos; // Positive Runway
			int32_t midiKnobMinPosDelta = midiKnobPos - knobMinPos; // Negative Runway

			// calculate change in value from midi controller
			// used to determine direction of midi controller value change (increase / decrease)
			int32_t midiKnobPosChange = midiKnobPos - previousKnobPosition;

			// we can only scale when we have a change in value
			if (midiKnobPosChange != 0) {
				float newKnobPosFloat = static_cast<float>(newKnobPos);
				float midiKnobPosFloat = static_cast<float>(midiKnobPos);
				float midiKnobPosChangeFloat = static_cast<float>(midiKnobPosChange);
				float midiKnobPosChangePercentage;

				// turning knob right
				if (midiKnobPosChange > 0) {
					if (midiKnobMaxPosDelta != 0) { // so we don't potentially divide by 0
						midiKnobPosChangePercentage = midiKnobPosChangeFloat / midiKnobMaxPosDelta;
						newKnobPosFloat = newKnobPosFloat + (delugeKnobMaxPosDelta * midiKnobPosChangePercentage);

						// make sure scaled value is not less than current value when we're turning right
						// (can happen when midi and deluge knob values get close)
						if (newKnobPosFloat < newKnobPos) {
							newKnobPosFloat = newKnobPos;
						}
					}
				}
				// turning knob left
				else {
					if (midiKnobMinPosDelta != 0) { // so we don't potentially divide by 0
						midiKnobPosChangePercentage = midiKnobPosChangeFloat / midiKnobMinPosDelta;
						newKnobPosFloat = newKnobPosFloat + (delugeKnobMinPosDelta * midiKnobPosChangePercentage);

						// make sure scaled value is not greater than current value when we're turning left
						// (can happen when midi and deluge knob values get close)
						if (newKnobPosFloat > newKnobPos) {
							newKnobPosFloat = newKnobPos;
						}
					}
				}
				newKnobPosFloat = std::round(newKnobPosFloat);
				newKnobPos = static_cast<int32_t>(newKnobPosFloat);
				newKnobPos = std::clamp(newKnobPos, -64_i32, 64_i32);
			}
		}

		savePreviousKnobPos(midiKnobPos, knob, doingMidiFollow, ccNumber);
	}

	return newKnobPos;
}

/// save the current midi knob position as the previous midi knob position so that it can be used next time the
/// takeover code is executed
void savePreviousKnobPos(int32_t knobPos, MIDIKnob* knob, bool doingMidiFollow, int32_t ccNumber) {
	if (knob != nullptr) {
		saveKnobPos(knobPos, knob);
	}
	else if (doingMidiFollow) {
		saveKnobPos(knobPos, ccNumber);
	}
}

// save previous knob position if regular midi learn is being used
void saveKnobPos(int32_t knobPos, MIDIKnob* knob) {
	knob->previousPosition = knobPos;
	knob->previousPositionSaved = true;
}

// save previous knob position if midi follow is being used
void saveKnobPos(int32_t knobPos, int32_t ccNumber) {
	midiFollow.previousKnobPos[ccNumber] = knobPos;
}

// returns previous knob position saved
// checks if a previous knob position has been saved, if not, it saves current midi knob position
int32_t getPreviousKnobPos(int32_t knobPos, MIDIKnob* knob, bool doingMidiFollow, int32_t ccNumber) {
	if (knob != nullptr) {
		if (!knob->previousPositionSaved) {
			saveKnobPos(knobPos, knob);
		}
		return knob->previousPosition;
	}
	else if (doingMidiFollow) {
		if (midiFollow.previousKnobPos[ccNumber] == kNoSelection) {
			saveKnobPos(knobPos, ccNumber);
		}
		return midiFollow.previousKnobPos[ccNumber];
	}
	return knobPos;
}
