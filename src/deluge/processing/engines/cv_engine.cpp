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
#include "RZA1/intc/devdrv_intc.h"
#include "definitions_cxx.hpp"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "processing/engines/audio_engine.h"
#include "util/comparison.h"
#include "util/functions.h"
#include <math.h>
#include <string.h>
// #include <algorithm>
#include "playback/playback_handler.h"
#include "timers_interrupts/timers_interrupts.h"

extern "C" {
#include "RZA1/gpio/gpio.h"

#include "RZA1/intc/devdrv_intc.h"
#include "RZA1/oled/oled_low_level.h"
#include "RZA1/rspi/rspi.h"
}

CVEngine cvEngine{};

CVEngine::CVEngine() {
	minGateOffTime = 10;
	clockState = false;
	mostRecentSwitchOffTimeOfPendingNoteOn = 0;
}

void CVEngine::init() {
	// As instructed by the AD DAC's datasheet, do the weird "linearity" routine
	/*
	IO::setOutputState(6, 13, 0);
	SPI::send(1, 0b00000101);
	SPI::send(1, 0b00000010); // LIN = 1
	SPI::send(1, 0);
	IO::setOutputState(6, 13, 1);
*/
	if (display->haveOLED()) {
		enqueueCVMessage(SPI_CHANNEL_CV, 0b00000101000000100000000000000000); // LIN = 1
	}
	else {
		R_RSPI_SendBasic32(SPI_CHANNEL_CV, 0b00000101000000100000000000000000); // LIN = 1
	}
	delayMS(10);

	/*
	IO::setOutputState(6, 13, 0);
	SPI::send(1, 0b00000101);
	SPI::send(1, 0b00000000); // LIN = 0
	SPI::send(1, 0);
	IO::setOutputState(6, 13, 1);
	*/
	if (display->haveOLED()) {
		enqueueCVMessage(SPI_CHANNEL_CV, 0b00000101000000000000000000000000); // LIN = 0
	}
	else {
		R_RSPI_SendBasic32(SPI_CHANNEL_CV, 0b00000101000000000000000000000000); // LIN = 0
	}

	for (int32_t i = 0; i < NUM_GATE_CHANNELS; i++) {

		// Setup gate outputs
		setPinAsOutput(gatePort[i], gatePin[i]);
	}

	// Switch all gate "off" to begin with - whatever "off" means
	updateGateOutputs();

	updateClockOutput();
	updateRunOutput();
}

// Gets called even for run and clock
void CVEngine::updateGateOutputs() {
	GateOutputs outputs = takePendingGateOutputs(true);
	if (outputs.mask == 0) {
		return;
	}
	CriticalSectionGuard guard;
	// These are newer than anything already scheduled for these channels
	AudioEngine::cancelScheduledGates(outputs.mask, guard);
	outputGates(outputs, guard);
}

GateOutputs CVEngine::takePendingGateOutputs(bool includeAsap) {
	GateOutputs outputs;
	outputs.mask = includeAsap ? pendingGates : (pendingGates & ~pendingAsapGates);
	if (outputs.mask == 0) {
		return outputs;
	}
	for (int32_t g = 0; g < NUM_GATE_CHANNELS; g++) {
		if (outputs.mask & (1 << g)) {
			outputs.levels |= gateLevel(g) << g;
		}
	}
	outputs.cvWaitMask = pendingNoteOnGates & outputs.mask;
	outputs.needsMinOffTime = ((pendingNoteOnGates | pendingAsapGates) & outputs.mask) != 0;

	pendingGates &= ~outputs.mask;
	pendingAsapGates &= ~outputs.mask;
	pendingNoteOnGates &= ~outputs.mask;
	return outputs;
}

// note or gate on the cv channel - if there's a cv out pending we send the gate after it finishes. This avoids a
// situation where the cv is delayed for an oled refresh and the gate gets sent first, causing an audible pitch
// correction
void CVEngine::outputGates(const GateOutputs& outputs, const InterruptsDisabled&) {
	uint8_t hold = cvOutPending ? outputs.cvWaitMask : 0;
	for (int32_t g = 0; g < NUM_GATE_CHANNELS; g++) {
		uint8_t bit = 1 << g;
		if (!(outputs.mask & bit)) {
			continue;
		}
		if (hold & bit) {
			gatesAwaitingCV |= bit;
			levelsAwaitingCV = (levelsAwaitingCV & ~bit) | (outputs.levels & bit);
		}
		else {
			gatesAwaitingCV &= ~bit;
			setOutputState(gatePort[g], gatePin[g], (outputs.levels & bit) != 0);
		}
	}
}

void CVEngine::switchGateNow(int32_t channel) {
	uint8_t bit = 1 << channel;
	CriticalSectionGuard guard;
	AudioEngine::cancelScheduledGates(bit, guard);
	pendingGates &= ~bit;
	pendingAsapGates &= ~bit;
	pendingNoteOnGates &= ~bit;
	gatesAwaitingCV &= ~bit;
	physicallySwitchGate(channel);
}

// These next two functions get called for run but not clock
void CVEngine::switchGateOff(int32_t channel) {
	gateChannels[channel].on = false;
	switchGateNow(channel);
	gateChannels[channel].timeLastSwitchedOff = AudioEngine::audioSampleTimer;
}

// In the future, it'd be great if manually-auditioned notes could supply doInstantlyIfPossible as true. Currently
// there's no infrastructure for an Instrument to know whether a note is manually auditioned.
void CVEngine::switchGateOn(int32_t channel, int32_t doInstantlyIfPossible) {
	gateChannels[channel].on = true;

	if (doInstantlyIfPossible) {
		uint32_t timeSinceLastSwitchedOff = AudioEngine::audioSampleTimer - gateChannels[channel].timeLastSwitchedOff;
		if (timeSinceLastSwitchedOff >= minGateOffTime * 441) {
			switchGateNow(channel);
			return;
		}
	}

	uint8_t bit = 1 << channel;
	pendingGates |= bit;
	if (doInstantlyIfPossible) {
		pendingAsapGates |= bit;
		pendingNoteOnGates &= ~bit;
	}
	else {
		pendingNoteOnGates |= bit;
		pendingAsapGates &= ~bit;
	}

	// If this gate was switched off more recently than any previous gate switch-off of a pending note-on, update the
	// running record of that
	if ((int32_t)(gateChannels[channel].timeLastSwitchedOff - mostRecentSwitchOffTimeOfPendingNoteOn) > 0) {
		mostRecentSwitchOffTimeOfPendingNoteOn = gateChannels[channel].timeLastSwitchedOff;
	}
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
			cvOutPending = true;
			sendVoltageOut(channel, voltage);
			switchGateOn(channel); // won't switch before the cv has been updated
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
	uint32_t output = (uint32_t)(0b00110000 | (1 << channel)) << 24;
	output |= (uint32_t)voltage << 8;
	// if we have a physical oled then we need to send via the pic
	if (deluge::hid::display::have_oled_screen) {
		enqueueCVMessage(channel, output);
	}
	else {
		R_RSPI_SendBasic32(SPI_CHANNEL_CV, output);
		cvOutPending = false;
	}
}

void CVEngine::physicallySwitchGate(int32_t channel) {
	setOutputState(gatePort[channel], gatePin[channel], gateLevel(channel));
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
	// If the previous clock edge was never taken for scheduled output then send it now, otherwise both would be merged
	// into one. Edges done during the audio window are taken (with their own time) as they happen, so this is rare.
	if (pendingGates & (1 << WHICH_GATE_OUTPUT_IS_CLOCK)) {
		switchGateNow(WHICH_GATE_OUTPUT_IS_CLOCK);
	}
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
	constexpr uint8_t clockBit = 1 << WHICH_GATE_OUTPUT_IS_CLOCK;
	if (pendingGates & clockBit) {
		D_PRINTLN("update clock while clock pending");
	}
	if (gateChannels[WHICH_GATE_OUTPUT_IS_CLOCK].mode != GateType::SPECIAL) {
		return;
	}

	gateChannels[WHICH_GATE_OUTPUT_IS_CLOCK].on = clockState;
	pendingGates |= clockBit;
	pendingAsapGates &= ~clockBit;
	pendingNoteOnGates &= ~clockBit;
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
void CVEngine::cvOutUpdated(const InterruptsDisabled& guard) {
	cvOutPending = false;
	// Only release gates whose scheduled time has already passed. Releasing every pending gate here would send them
	// up to a whole audio buffer early whenever the CV word finished before the MIDI/gate timer fired (e.g. when an
	// OLED refresh delayed the CV), and would also flush pending clock pulses early.
	if (gatesAwaitingCV) {
		GateOutputs held;
		held.mask = gatesAwaitingCV;
		held.levels = levelsAwaitingCV;
		outputGates(held, guard);
	}
}

extern "C" {
// Called both from the CV SPI ISR and from the main loop
void cvSent() {
	if (intc_func_active != 0) {
		ISRCriticalSectionGuard guard;
		cvEngine.cvOutUpdated(guard);
	}
	else {
		CriticalSectionGuard guard;
		cvEngine.cvOutUpdated(guard);
	}
}
}