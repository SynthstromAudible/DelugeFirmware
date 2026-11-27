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
#include <libdeluge/timed_output.h>

#define WHICH_GATE_OUTPUT_IS_RUN 2
#define WHICH_GATE_OUTPUT_IS_CLOCK 3

// (The gate output pin map moved to the BSP's cv_gate implementation.)

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
	/// The play-timeline stamp of the frame playing when the gate last switched off (DELUGE_STAMP_NOW before then):
	/// the next switch-on waits minGateOffTime after it
	uint32_t offStamp{DELUGE_STAMP_NOW};
};

/// The CV and gate outputs' state and policy: notes to voltages, the clock and run gates, and the minimum gate off
/// time. Output goes through libdeluge/timed_output.h at the render's stamp (see AudioEngine::cvGateStamp()), so a
/// note's CV reaches the DAC before its gate switches.
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

	void analogOutTick();
	void playbackBegun();
	void playbackEnded();
	/// Send the clock gate's state, when the clock gate carries the clock
	void updateClockOutput();
	void updateRunOutput();
	bool isTriggerClockOutputEnabled();

	/// The stamp a gate switching on at @p requested goes at: no earlier than @p minGateOffTime (100 µs units) after
	/// the stamp it last switched off at, @p lastOff. DELUGE_STAMP_NOW for @p lastOff, or an off more than a second
	/// behind @p playNow (the stamp playing now), means no off to wait for; DELUGE_STAMP_NOW for @p requested means
	/// as soon as the off time allows. Never DELUGE_STAMP_NOW unless @p requested is and there is no off to wait for.
	static uint32_t onStampAfterOff(uint32_t requested, uint32_t lastOff, uint32_t playNow, uint8_t minGateOffTime);

	GateChannel gateChannels[NUM_GATE_CHANNELS];

	CVChannel cvChannels[NUM_PHYSICAL_CV_CHANNELS];

	uint8_t minGateOffTime; // in 100uS's

	bool clockState;

	void sendVoltageOut(uint8_t channel, uint16_t voltage);

	inline bool isNoteOn(int32_t channel, int32_t note) {
		return (gateChannels[channel].on && cvChannels[channel].noteCurrentlyPlaying == note);
	}

private:
	void recalculateCVChannelVoltage(uint8_t channel);
	/// Switch a gate off at once, a head start on the off time before it switches on again
	void switchGateOff(int32_t channel);
	/// Switch a gate on at the render's stamp, or as soon as possible, once the minimum off time has passed
	void switchGateOn(int32_t channel, bool asSoonAsPossible = false);
	/// Switch a gate to its state at once, dropping any edge queued for it and not waiting for its CV
	void switchGateNow(int32_t channel);
	uint8_t gateLevel(int32_t channel) const {
		// The gate pin is inverted - setting it true turns the gate off
		return gateChannels[channel].on == (gateChannels[channel].mode == GateType::S_TRIG);
	}
};

extern CVEngine cvEngine;
