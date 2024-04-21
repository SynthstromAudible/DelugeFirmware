/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"

extern "C" {
#include "RZA1/uart/sio_char.h"
}

using namespace deluge;
using namespace gui;

MidiTakeover midiTakeover{};

// initialize variables
MidiTakeover::MidiTakeover() {
}

/// based on the midi takeover default setting of JUMP, PICKUP, or SCALE
/// this function will calculate the knob position that the deluge parameter that the midi cc
/// received is learned to should be set at based on the midi cc value received
int32_t MidiTakeover::calculateKnobPos(ModelStackWithAutoParam* modelStackWithParam, int32_t knobPos, int32_t value,
                                       MIDIKnob* knob, bool doingMidiFollow, int32_t ccNumber) {
	/*

	Step #1: Convert Midi Controller's CC Value to Deluge Knob Position Value

	- Midi CC Values for non endless encoders typically go from 0 to 127
	- Deluge Knob Position Value goes from -64 to 64

	To convert Midi CC Value to Deluge Knob Position Value, subtract 64 from the Midi CC Value

	So a Midi CC Value of 0 is equal to a Deluge Knob Position Value of -64 (0 less 64).

	Similarly a Midi CC Value of 127 is equal to a Deluge Knob Position Value of +63 (127 less 64)

	*/

	int32_t midiKnobPos = 64;
	if (value < kMaxMIDIValue) {
		midiKnobPos = value - 64;
	}

	int32_t newKnobPos = 0;

	if (midiEngine.midiTakeover == MIDITakeoverMode::JUMP) { // Midi Takeover Mode = Jump
		newKnobPos = midiKnobPos;
		if (knob != nullptr) {
			knob->previousPosition = midiKnobPos;
		}
		else if (doingMidiFollow) {
			midiFollow.previousKnobPos[ccNumber] = midiKnobPos;
		}
	}
	else { // Midi Takeover Mode = Pickup or Value Scaling
		// Save previous knob position for first time
		// The first time a midi knob is turned in a session, no previous midi knob position information exists, so to
		// start, it will be equal to the current midiKnobPos This code is also executed when takeover mode is changed
		// to Jump and back to Pickup/Scale because in Jump mode no previousPosition information gets saved

		if (knob != nullptr) {
			if (!knob->previousPositionSaved) {
				knob->previousPosition = midiKnobPos;

				knob->previousPositionSaved = true;
			}
		}
		else if (doingMidiFollow) {
			if (midiFollow.previousKnobPos[ccNumber] == kNoSelection) {
				midiFollow.previousKnobPos[ccNumber] = midiKnobPos;
			}
		}

		// adjust previous knob position saved

		// Here we check to see if the midi knob position previously saved is greater or less than the current midi knob
		// position +/- 1 If it's by more than 1, the previous position is adjusted. This could happen for example if
		// you changed banks and the previous position is no longer valid. By resetting the previous position we ensure
		// that the there isn't unwanted jumpyness in the calculation of the midi knob position change amount
		if (knob != nullptr) {
			if (knob->previousPosition > (midiKnobPos + 1) || knob->previousPosition < (midiKnobPos - 1)) {

				knob->previousPosition = midiKnobPos;
			}
		}
		else if (doingMidiFollow) {
			int32_t previousPosition = midiFollow.previousKnobPos[ccNumber];
			if (previousPosition > (midiKnobPos + 1) || previousPosition < (midiKnobPos - 1)) {

				midiFollow.previousKnobPos[ccNumber] = midiKnobPos;
			}
		}

		// Here is where we check if the Knob/Fader on the Midi Controller is out of sync with the Deluge Knob Position

		// First we check if the Midi Knob/Fader is sending a Value that is greater than or less than the current Deluge
		// Knob Position by a max difference of +/- kMIDITakeoverKnobSyncThreshold If the difference is greater than
		// kMIDITakeoverKnobSyncThreshold, ignore the CC value change (or scale it if value scaling is on)
		int32_t midiKnobMinPos = knobPos - kMIDITakeoverKnobSyncThreshold;
		int32_t midiKnobMaxPos = knobPos + kMIDITakeoverKnobSyncThreshold;

		if ((midiKnobMinPos <= midiKnobPos) && (midiKnobPos <= midiKnobMaxPos)) {
			newKnobPos = knobPos + (midiKnobPos - knobPos);
		}
		else {
			// if the above conditions fail and pickup mode is enabled, then the Deluge Knob Position (and therefore the
			// Parameter Value with it) remains unchanged
			if (midiEngine.midiTakeover == MIDITakeoverMode::PICKUP) { // Midi Pickup Mode On
				newKnobPos = knobPos;
			}
			// if the first two conditions fail and value scaling mode is enabled, then the Deluge Knob Position is
			// scaled upwards or downwards based on relative positions of Midi Controller Knob and Deluge Knob to
			// min/max of knob range.
			else { // Midi Value Scaling Mode On
				// Set the max and min of the deluge midi knob position range
				int32_t knobMaxPos = 64;
				int32_t knobMinPos = -64;

				// calculate amount of deluge "knob runway" is remaining from current knob position to max and min of
				// knob position range
				int32_t delugeKnobMaxPosDelta = knobMaxPos - knobPos; // Positive Runway
				int32_t delugeKnobMinPosDelta = knobPos - knobMinPos; // Negative Runway

				// calculate amount of midi "knob runway" is remaining from current knob position to max and min of knob
				// position range
				int32_t midiKnobMaxPosDelta = knobMaxPos - midiKnobPos; // Positive Runway
				int32_t midiKnobMinPosDelta = midiKnobPos - knobMinPos; // Negative Runway

				// calculate by how much the current midiKnobPos has changed from the previous midiKnobPos recorded
				int32_t midiKnobPosChange = 0;
				if (knob != nullptr) {
					midiKnobPosChange = midiKnobPos - knob->previousPosition;
				}
				else if (doingMidiFollow) {
					midiKnobPosChange = midiKnobPos - midiFollow.previousKnobPos[ccNumber];
				}

				// Set fixed point variable which will be used calculate the percentage in midi knob position
				int32_t midiKnobPosChangePercentage;

				// if midi knob position change is greater than 0, then the midi knob position has increased (e.g.
				// turned knob right)
				if (midiKnobPosChange > 0) {
					// fixed point math calculation of new deluge knob position when midi knob position has increased

					midiKnobPosChangePercentage = (midiKnobPosChange << 20) / midiKnobMaxPosDelta;

					newKnobPos = knobPos + ((delugeKnobMaxPosDelta * midiKnobPosChangePercentage) >> 20);
				}
				// if midi knob position change is less than 0, then the midi knob position has decreased (e.g. turned
				// knob left)
				else if (midiKnobPosChange < 0) {
					// fixed point math calculation of new deluge knob position when midi knob position has decreased

					midiKnobPosChangePercentage = (midiKnobPosChange << 20) / midiKnobMinPosDelta;

					newKnobPos = knobPos + ((delugeKnobMinPosDelta * midiKnobPosChangePercentage) >> 20);
				}
				// if midi knob position change is 0, then the midi knob position has not changed and thus no change in
				// deluge knob position / parameter value is required
				else {
					newKnobPos = knobPos;
				}
			}
		}

		// save the current midi knob position as the previous midi knob position so that it can be used next time the
		// takeover code is executed
		if (knob != nullptr) {
			knob->previousPosition = midiKnobPos;
		}
		else if (doingMidiFollow) {
			midiFollow.previousKnobPos[ccNumber] = midiKnobPos;
		}
	}

	return newKnobPos;
}
