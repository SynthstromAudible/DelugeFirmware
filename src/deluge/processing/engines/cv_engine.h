/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "model/drum/gate_drum.h"
#include <cstdint>

#define WHICH_GATE_OUTPUT_IS_RUN 2
#define WHICH_GATE_OUTPUT_IS_CLOCK 3

const uint8_t gatePort[] = {2, 2, 2, 4};
const uint8_t gatePin[] = {7, 8, 9, 0};

class CVChannel {
public:
	CVChannel() {
		noteCurrentlyPlaying = -32768;
		voltsPerOctave = 100;
		transpose = 0;
		cents = 0;
		pitchBend = 0;
	}
	int16_t noteCurrentlyPlaying;
	uint8_t voltsPerOctave;
	int8_t transpose;
	int8_t cents;
	int32_t
	    pitchBend; // (1 << 23) represents one semitone. So full 32-bit range can be +-256 semitones. This is different
	               // to the equivalent calculation in Voice, which needs to get things into a number of octaves.
};

class GateChannel {
public:
	GateChannel() { on = false; }
	bool on; // Means either on now, or "awaiting" switch-on
	GateType mode;
	uint32_t timeLastSwitchedOff;
};

class CVEngine {
public:
	CVEngine();
	void init();
	void sendNote(bool on, uint8_t channel, int16_t note = -32768);
	void setGateType(uint8_t whichGate, GateType value);
	void setCVVoltsPerOctave(uint8_t channel, uint8_t value);
	void setCVTranspose(uint8_t channel, int32_t semitones, int32_t cents);
	void setCVPitchBend(uint8_t channel, int32_t value, bool outputToo = true);
	int32_t calculateVoltage(int32_t note, uint8_t channel);
	void physicallySwitchGate(int32_t channel);
	// defer updating the gate while CV is pending and do it when it's done
	void cvOutUpdated();

	void analogOutTick();
	void playbackBegun();
	void playbackEnded();
	/// toggles clock, does not physically update until updateGateOutputs called
	void updateClockOutput();
	void updateRunOutput();
	bool isTriggerClockOutputEnabled();
	/// physically send all gate outs if any output pending
	void updateGateOutputs();

	bool isGatePending() const { return gateOutputPending; }
	bool isRunPending() const { return asapGateOutputPending; }
	bool isClockPending() const { return clockOutputPending; }
	bool isAnythingButRunPending() const { return isGatePending() || isClockPending(); }
	bool isAnythingPending() const { return isGatePending() || isClockPending() || isRunPending(); }
	GateChannel gateChannels[NUM_GATE_CHANNELS];

	CVChannel cvChannels[NUM_PHYSICAL_CV_CHANNELS];

	uint8_t minGateOffTime; // in 100uS's

	bool clockState;

	// When one or more note-on is pending, this is the latest time that one of them last switched off.
	// But it seems I only use this very coarsely - more to see if we're still in the same audio frame than to measure
	// time exactly. This could be improved.
	uint32_t mostRecentSwitchOffTimeOfPendingNoteOn;

	void sendVoltageOut(uint8_t channel, uint16_t voltage);

	inline bool isNoteOn(int32_t channel, int32_t note) {
		return (gateChannels[channel].on && cvChannels[channel].noteCurrentlyPlaying == note);
	}

private:
	void recalculateCVChannelVoltage(uint8_t channel);
	void switchGateOff(int32_t channel);
	void switchGateOn(int32_t channel, int32_t doInstantlyIfPossible = false);
	/// signifies there's a gate that can't go until the cv is output
	bool cvOutPending{false};
	/// gate 1-4 as synths or drums
	bool gateOutputPending{false};
	/// gate 3 as a run signal
	bool asapGateOutputPending;
	/// gate 4 as a clock signal
	bool clockOutputPending;
};

extern CVEngine cvEngine;
