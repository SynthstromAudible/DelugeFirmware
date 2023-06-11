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

#include <AudioEngine.h>
#include <sound.h>
#include "envelope.h"
#include "voice.h"
#include "uart.h"

Envelope::Envelope() {
}

int32_t Envelope::render(uint32_t numSamples, uint32_t attack, uint32_t decay, uint32_t sustain, uint32_t release,
                         const uint16_t* releaseTable) {

considerEnvelopeStage:
	switch (state) {
	case ENVELOPE_STAGE_ATTACK:
		pos +=
		    attack
		    * numSamples; // Increment the pos *before* taking a value, so we can skip the attack section entirely with a high posIncrease
		if (pos >= 8388608) {
			pos = 0;
			setState(ENVELOPE_STAGE_DECAY);
			goto considerEnvelopeStage;
		}
		//lastValue = pos << 8;
		lastValue = 2147483647 - getDecay4(pos, 23); // Makes curved attack
		lastValue = getMax(lastValue, (int32_t)1);
		break;

	case ENVELOPE_STAGE_DECAY:
		lastValue = sustain + (uint32_t)multiply_32x32_rshift32(getDecay8(pos, 23), 2147483647 - sustain) * 2;

		pos += decay * numSamples;

		if (pos >= 8388608) {

			// If sustain is 0, we may as well be switched off already
			if (sustain == 0) {
				setState(ENVELOPE_STAGE_OFF);
			}
			else {
				setState(ENVELOPE_STAGE_SUSTAIN);
			}
		}

		break;

	case ENVELOPE_STAGE_SUSTAIN:
		lastValue = sustain;
		if (ignoredNoteOff) unconditionalRelease();
		break;

	case ENVELOPE_STAGE_RELEASE:
		pos += release * numSamples;
		if (pos >= 8388608) {
			setState(ENVELOPE_STAGE_OFF);
			lastValue = 0;
			return -2147483648;
		}
		//int32_t thisValue = multiply_32x32_rshift32(getDecay8(pos, 23), lastValue) << 1;
		//int32_t negativePos = (8388608 - pos) >> 13;
		//int32_t thisValue = multiply_32x32_rshift32(negativePos * negativePos * negativePos, lastValue) << 2;
		lastValue = multiply_32x32_rshift32(interpolateTable(pos, 23, releaseTable), lastValuePreCurrentStage) << 1;
		break;

	case ENVELOPE_STAGE_FAST_RELEASE:
		pos += fastReleaseIncrement * numSamples;
		if (pos >= 8388608) {
			setState(ENVELOPE_STAGE_OFF);
			return -2147483648;
		}

		//lastValue = multiply_32x32_rshift32((8388608 - pos) << 8, lastValuePreRelease) << 1;

		// This alternative line does the release in a sine shape, which you'd think would cause less high-frequency content than the above kinda "triangle" one, but it sounds about the same somehow
		// Actually no it does sound a bit better for Kody's deep bass sample
		lastValue =
		    multiply_32x32_rshift32((getSine(pos + (8388608 >> 1), 24) >> 1) + 1073741824, lastValuePreCurrentStage)
		    << 1;
		break;

	default: // OFF
		return -2147483648;
	}

	return (lastValue - 1073741824) << 1; // Centre the range of the envelope around 0
}

int32_t Envelope::noteOn(bool directlyToDecay) {
	ignoredNoteOff = false;
	pos = 0;
	if (!directlyToDecay) {
		setState(ENVELOPE_STAGE_ATTACK);
		lastValue = 0;
	}
	else {
		setState(ENVELOPE_STAGE_DECAY);
		lastValue = 2147483647;
	}

	return (lastValue - 1073741824) << 1; // Centre the range of the envelope around 0
}

int32_t Envelope::noteOn(uint8_t envelopeIndex, Sound* sound, Voice* voice) {
	int32_t attack = voice->paramFinalValues[PARAM_LOCAL_ENV_0_ATTACK + envelopeIndex];

	bool directlyToDecay = (attack > 245632);

	return noteOn(directlyToDecay);
}

void Envelope::noteOff(uint8_t envelopeIndex, Sound* sound, ParamManagerForTimeline* paramManager) {

	if (!sound->envelopeHasSustainCurrently(envelopeIndex, paramManager)) {
		ignoredNoteOff = true;
	}
	else if (
	    state
	    < ENVELOPE_STAGE_RELEASE) { // Could we ever have already been in release state? Probably not, but just in case
		unconditionalRelease();
	}
}

void Envelope::setState(uint8_t newState) {
	state = newState;
	timeEnteredState = AudioEngine::nextVoiceState++;
}

void Envelope::unconditionalRelease(uint8_t typeOfRelease, uint32_t newFastReleaseIncrement) {
	setState(typeOfRelease);
	pos = 0;
	lastValuePreCurrentStage = lastValue;

	if (typeOfRelease == ENVELOPE_STAGE_FAST_RELEASE) fastReleaseIncrement = newFastReleaseIncrement;
}

void Envelope::resumeAttack(int32_t oldLastValue) {
	if (state == ENVELOPE_STAGE_ATTACK) {
		pos = interpolateTableInverse(2147483647 - oldLastValue, 23, decayTableSmall4);
	}
}
