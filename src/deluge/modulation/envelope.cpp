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

#include "modulation/envelope.h"
#include "definitions_cxx.hpp"
#include "model/voice/voice.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"

namespace params = deluge::modulation::params;

Envelope::Envelope() {
}

int32_t Envelope::render(uint32_t numSamples, uint32_t attack, uint32_t decay, uint32_t sustain, uint32_t release,
                         const uint16_t* releaseTable) {

considerEnvelopeStage:
	switch (state) {
	case EnvelopeStage::ATTACK:
		pos += attack * numSamples; // Increment the pos *before* taking a value, so we can skip the attack section
		                            // entirely with a high posIncrease
		if (pos >= 8388608) {
			pos = 0;
			setState(EnvelopeStage::DECAY);
			goto considerEnvelopeStage;
		}
		// lastValue = pos << 8;
		lastValue = 2147483647 - getDecay4(pos, 23); // Makes curved attack
		lastValue = std::max(lastValue, (int32_t)1);
		break;
		// TODO: this would be cool
		//	case EnvelopeStage::HOLD:
		//		lastValue = 2147483647;
		//		break;
	case EnvelopeStage::DECAY:
		// otherwise modulating sustain with aftertouch causes un smoothed jumps downstream because params don't smooth
		// the envelope value and envelopes are rendered before other params are  calculated (including the envelopes
		// own sustain). To fix this properly we'd first patch and smooth expression to the envelope, then render the
		// envelope, then render everything else, but that seems too complicated right now. This works and doesn't make
		// a huge difference since we'd usually need to smooth it anyway

		smoothedSustain = add_saturation(smoothedSustain, numSamples * (((int32_t)sustain - smoothedSustain) >> 9));
		lastValue = smoothedSustain + multiply_32x32_rshift32(getDecay8(pos, 23), 2147483647 - smoothedSustain) * 2;

		pos += decay * numSamples;

		if (pos >= 8388608) {
			setState(EnvelopeStage::SUSTAIN);
		}

		break;

	case EnvelopeStage::SUSTAIN:
		smoothedSustain = add_saturation(smoothedSustain, numSamples * (((int32_t)sustain - smoothedSustain) >> 9));
		lastValue = smoothedSustain;
		if (ignoredNoteOff) {
			unconditionalRelease();
		}
		break;

	case EnvelopeStage::RELEASE:
		pos += release * numSamples;
		if (pos >= 8388608) {
			setState(EnvelopeStage::OFF);
			lastValue = 0;
			return -2147483648;
		}
		// int32_t thisValue = multiply_32x32_rshift32(getDecay8(pos, 23), lastValue) << 1;
		// int32_t negativePos = (8388608 - pos) >> 13;
		// int32_t thisValue = multiply_32x32_rshift32(negativePos * negativePos * negativePos, lastValue) << 2;
		lastValue = multiply_32x32_rshift32(interpolateTable(pos, 23, releaseTable), lastValuePreCurrentStage) << 1;
		break;

	case EnvelopeStage::FAST_RELEASE:
		if (fastReleaseIncrement < 2 * release) {
			release = 2 * release;
			fastReleaseIncrement = release;
		}
		pos += fastReleaseIncrement * numSamples;
		if (pos >= 8388608) {
			setState(EnvelopeStage::OFF);
			return -2147483648;
		}

		// lastValue = multiply_32x32_rshift32((8388608 - pos) << 8, lastValuePreRelease) << 1;

		// This alternative line does the release in a sine shape, which you'd think would cause less high-frequency
		// content than the above kinda "triangle" one, but it sounds about the same somehow Actually no it does sound a
		// bit better for Kody's deep bass sample
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
		setState(EnvelopeStage::ATTACK);
		lastValue = 0;
	}
	else {
		setState(EnvelopeStage::DECAY);
		lastValue = 2147483647;
	}

	return (lastValue - 1073741824) << 1; // Centre the range of the envelope around 0
}

int32_t Envelope::noteOn(uint8_t envelopeIndex, Sound* sound, Voice* voice) {
	int32_t attack = voice->paramFinalValues[params::LOCAL_ENV_0_ATTACK + envelopeIndex];
	smoothedSustain = voice->paramFinalValues[params::LOCAL_ENV_0_SUSTAIN + envelopeIndex];
	bool directlyToDecay = (attack > 245632);

	return noteOn(directlyToDecay);
}

void Envelope::noteOff(uint8_t envelopeIndex, Sound* sound, ParamManagerForTimeline* paramManager) {

	if (!sound->envelopeHasSustainCurrently(envelopeIndex, paramManager)) {
		ignoredNoteOff = true;
	}
	else if (state < EnvelopeStage::RELEASE) { // Could we ever have already been in release state? Probably not, but
		                                       // just in case
		unconditionalRelease();
	}
}
/// resets time to give drone notes a lower likelihood of being culled, since they'll stay in sustain as they loop
void Envelope::resetTimeEntered() {
	if (state == EnvelopeStage::SUSTAIN) {
		timeEnteredState = AudioEngine::nextVoiceState++;
	}
}

void Envelope::setState(EnvelopeStage newState) {
	state = newState;
	timeEnteredState = AudioEngine::nextVoiceState++;
}

void Envelope::unconditionalOff() {
	lastValuePreCurrentStage = lastValue;
	setState(EnvelopeStage::OFF);
}

void Envelope::unconditionalRelease(EnvelopeStage typeOfRelease, uint32_t newFastReleaseIncrement) {
	if (state != typeOfRelease) {
		setState(typeOfRelease);
		pos = 0;
		lastValuePreCurrentStage = lastValue;
	}

	if (typeOfRelease == EnvelopeStage::FAST_RELEASE) {
		fastReleaseIncrement = newFastReleaseIncrement;
	}
}

void Envelope::resumeAttack(int32_t oldLastValue) {
	if (state == EnvelopeStage::ATTACK) {
		pos = interpolateTableInverse(2147483647 - oldLastValue, 23, decayTableSmall4);
	}
}
