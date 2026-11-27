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

#include "processing/engines/cv_engine.h"
#include "definitions_cxx.hpp"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "libdeluge/cv_gate.h"
#include "libdeluge/timed_output.h"
#include "processing/engines/audio_engine.h"
#include "util/comparison.h"
#include "util/functions.h"
#include <math.h>
#include <string.h>
// #include <algorithm>
#include "playback/playback_handler.h"
#include "playback/trigger_clock_input.h"

CVEngine cvEngine{};

CVEngine::CVEngine() {
	minGateOffTime = 10;
	clockState = false;
}

void CVEngine::init() {
	// CV DAC linearity init + gate pin setup are board concerns (the CV DAC shares
	// SPI with the OLED; pass whether a display is present so writes are routed
	// correctly).
	deluge_cv_init(true);
	deluge_gate_init();
	deluge::playback::installTriggerClockInput();

	updateClockOutput();
	updateRunOutput();
}

uint32_t CVEngine::onStampAfterOff(uint32_t requested, uint32_t lastOff, uint8_t minGateOffTime) {
	if (lastOff == DELUGE_STAMP_NOW) {
		return requested;
	}
	// 4.41 frames per 100 µs, Q16
	uint32_t earliest = lastOff + ((static_cast<uint32_t>(minGateOffTime) * 289014) >> 16);
	if (requested == DELUGE_STAMP_NOW) {
		return earliest;
	}
	return static_cast<int32_t>(requested - earliest) >= 0 ? requested : earliest;
}

void CVEngine::switchGateNow(int32_t channel) {
	deluge_out_gate_cancel(1 << channel);
	(void)deluge_out_gate_at(channel, gateLevel(channel), DELUGE_STAMP_NOW);
}

// These next two functions get called for run but not clock
void CVEngine::switchGateOff(int32_t channel) {
	gateChannels[channel].on = false;
	switchGateNow(channel);
	gateChannels[channel].offStamp = deluge_audio_play_stamp();
}

void CVEngine::switchGateOn(int32_t channel, bool asSoonAsPossible) {
	gateChannels[channel].on = true;
	uint32_t requested = asSoonAsPossible ? DELUGE_STAMP_NOW : AudioEngine::cvGateStamp();
	(void)deluge_out_gate_at(channel, gateLevel(channel),
	                         onStampAfterOff(requested, gateChannels[channel].offStamp, minGateOffTime));
}

// note -32768 means switch "all notes off", or switch on without changing actual CV voltage output
void CVEngine::sendNote(bool on, uint8_t channel, int16_t note) {

	// If this gate channel is reserved for a special purpose, don't do anything
	if (gateChannels[channel].mode == GateType::SPECIAL) {
		return;
	}

	// Note-off
	if (!on) {

		// Switch off, unless the note that's playing is a different one (i.e. if a new one had already cut short this
		// one that we're now saying we wanted to stop)
		if (gateChannels[channel].on
		    && (channel >= NUM_PHYSICAL_CV_CHANNELS || note == ALL_NOTES_OFF
		        || cvChannels[channel].noteCurrentlyPlaying == note)) {

			// Physically switch it right now, to get a head-start before it turns back on
			switchGateOff(channel);
		}
	}

	// Note-on
	else {

		int32_t voltage;

		// If it's not a gate-only note-on (and there's a physical CV output for this channel)...
		if (note != ALL_NOTES_OFF && channel < NUM_PHYSICAL_CV_CHANNELS) {

			// Calculate the voltage
			voltage = calculateVoltage(note, channel);
			voltage = std::min(voltage, (int32_t)65535);
			voltage = std::max(voltage, (int32_t)0);
			sendVoltageOut(channel, voltage);
			switchGateOn(channel); // at the same stamp, so the gate waits for its voltage
		}
		else {
			switchGateOn(channel);
		}

		if (note != ALL_NOTES_OFF && channel < NUM_PHYSICAL_CV_CHANNELS) {
			cvChannels[channel].noteCurrentlyPlaying = note;
		}
	}
}

void CVEngine::sendVoltageOut(uint8_t channel, uint16_t voltage) {
	(void)deluge_out_cv_at(channel, voltage, AudioEngine::cvGateStamp());
}

void CVEngine::setCVVoltsPerOctave(uint8_t channel, uint8_t value) {
	cvChannels[channel].voltsPerOctave = value;
	recalculateCVChannelVoltage(channel);
}

void CVEngine::setCVTranspose(uint8_t channel, int32_t semitones, int32_t cents) {
	cvChannels[channel].transpose = semitones;
	cvChannels[channel].cents = cents;
	recalculateCVChannelVoltage(channel);
}

void CVEngine::setCVPitchBend(uint8_t channel, int32_t value, bool outputToo) {
	cvChannels[channel].pitchBend = value;
	if (outputToo) {
		recalculateCVChannelVoltage(channel);
	}
}

// Does it even if the corresponding gate isn't "on", because the note might still be audible on the connected physical
// synth
void CVEngine::recalculateCVChannelVoltage(uint8_t channel) {
	int32_t voltage = calculateVoltage(cvChannels[channel].noteCurrentlyPlaying, channel);

	voltage = std::min(voltage, (int32_t)65535);
	voltage = std::max(voltage, (int32_t)0);
	sendVoltageOut(channel, voltage);
}

// Represents 1V as 6552. So 10V is 65520.
int32_t CVEngine::calculateVoltage(int32_t note, uint8_t channel) {
	double transposedNoteCode = (double)(note + cvChannels[channel].transpose)
	                            + (double)cvChannels[channel].cents * 0.01
	                            + (double)cvChannels[channel].pitchBend / (1 << 23);

	int32_t voltage;
	// Hz per volt
	if (cvChannels[channel].voltsPerOctave == 0) {
		voltage = round(pow((double)2, (transposedNoteCode - 60) / 12)
		                * 6552); // Puts middle C at 1V - I think? Would 2V be better?
	}

	// Volts per octave
	else {
		voltage =
		    (transposedNoteCode - 24) * 5.46 * cvChannels[channel].voltsPerOctave
		    + 0.5; // The 0.5 rounds it. And it's 5.46 rather than 546 because voltsPerOctave is in 0.01's of a volt.
	}

	return voltage;
}

void CVEngine::analogOutTick() {
	clockState = !clockState;
	updateClockOutput();
}

void CVEngine::playbackBegun() {
	clockState = false;
	updateRunOutput();
}

void CVEngine::playbackEnded() {
	clockState = false;
	updateClockOutput();
	updateRunOutput();
}

void CVEngine::setGateType(uint8_t channel, GateType value) {
	GateType oldValue = gateChannels[channel].mode;

	gateChannels[channel].mode = value;

	// We now need to update the output's physical status

	// If it's been set to a "special" type...
	if (value == GateType::SPECIAL) {

		// Clock
		if (channel == WHICH_GATE_OUTPUT_IS_CLOCK) {
			if (playbackHandler.isInternalClockActive()) {
				playbackHandler.resyncAnalogOutTicksToInternalTicks();
			}
			updateClockOutput();
		}

		// Run
		else if (channel == WHICH_GATE_OUTPUT_IS_RUN) {
			updateRunOutput();
		}
	}

	else {
		switchGateNow(channel);

		if (oldValue == GateType::SPECIAL) {
			// If we just stopped clock output...
			if (channel == WHICH_GATE_OUTPUT_IS_CLOCK) {
				playbackHandler.triggerClockOutTickScheduled = false;
			}
		}
	}
}

void CVEngine::updateClockOutput() {
	if (gateChannels[WHICH_GATE_OUTPUT_IS_CLOCK].mode != GateType::SPECIAL) {
		return;
	}
	gateChannels[WHICH_GATE_OUTPUT_IS_CLOCK].on = clockState;
	// A trigger, so no minimum off time
	(void)deluge_out_gate_at(WHICH_GATE_OUTPUT_IS_CLOCK, gateLevel(WHICH_GATE_OUTPUT_IS_CLOCK),
	                         AudioEngine::cvGateStamp());
}

void CVEngine::updateRunOutput() {
	if (gateChannels[WHICH_GATE_OUTPUT_IS_RUN].mode != GateType::SPECIAL) {
		return;
	}

	bool runState = (playbackHandler.isEitherClockActive() && !playbackHandler.ticksLeftInCountIn);

	if (runState) {
		switchGateOn(WHICH_GATE_OUTPUT_IS_RUN,
		             true); // Try to do instantly, because it's actually good if RUN can switch on before the first
		                    // clock is sent
	}
	else {
		switchGateOff(WHICH_GATE_OUTPUT_IS_RUN);
	}
}

bool CVEngine::isTriggerClockOutputEnabled() {
	return (gateChannels[WHICH_GATE_OUTPUT_IS_CLOCK].mode == GateType::SPECIAL);
}
