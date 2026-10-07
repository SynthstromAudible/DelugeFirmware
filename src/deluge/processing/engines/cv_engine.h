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

/// A snapshot of gate output changes, so they can be physically output at a later time (e.g. from the MIDI/gate
/// output ISR) without being affected by changes made in the meantime
struct GateOutputs {
	uint8_t mask{0};             ///< bitmask of gate channels to physically switch
	uint8_t levels{0};           ///< pin level for each channel in mask
	uint8_t cvWaitMask{0};       ///< channels in mask which can't switch until any pending CV has been sent
	bool needsMinOffTime{false}; ///< contains a note-on which must respect minGateOffTime
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
	// release any gates which were held while CV is pending now that it's done
	void cvOutUpdated();

	void analogOutTick();
	void playbackBegun();
	void playbackEnded();
	/// toggles clock, does not physically update until updateGateOutputs called
	void updateClockOutput();
	void updateRunOutput();
	bool isTriggerClockOutputEnabled();
	/// physically send all pending gate outs right now
	void updateGateOutputs();
	/// take all gate changes made since the last call, so they can be scheduled to go out at a specific time.
	/// asap (run) gates are only included if includeAsap is set, since they need to respect minGateOffTime
	GateOutputs takePendingGateOutputs(bool includeAsap);
	/// physically output a snapshot of gates. Safe to call from an ISR
	void outputGates(const GateOutputs& outputs);

	bool isAnythingButRunPending() const { return (pendingGates & ~pendingAsapGates) != 0; }
	bool isAnythingPending() const { return pendingGates != 0; }
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
	/// physically switch a gate right now, superseding any pending or scheduled output for it
	void switchGateNow(int32_t channel);
	uint8_t gateLevel(int32_t channel) const {
		// setOutputState is inverted - sending true turns the gate off
		return gateChannels[channel].on == (gateChannels[channel].mode == GateType::S_TRIG);
	}
	/// signifies there's a gate that can't go until the cv is output
	bool cvOutPending{false};
	/// bitmask of gate channels which have changed but haven't been taken for output yet
	uint8_t pendingGates{0};
	/// subset of pendingGates which should go asap (run signal), respecting minGateOffTime
	uint8_t pendingAsapGates{0};
	/// subset of pendingGates which are deferred note-ons (must wait for CV and respect minGateOffTime)
	uint8_t pendingNoteOnGates{0};
	/// gates whose scheduled time has passed but are being held until their cv is output
	uint8_t gatesAwaitingCV{0};
	uint8_t levelsAwaitingCV{0};
};

extern CVEngine cvEngine;
