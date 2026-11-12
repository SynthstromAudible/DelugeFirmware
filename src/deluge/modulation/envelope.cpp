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
#include "util/fixedpoint.h"
#include <limits>

namespace params = deluge::modulation::params;

Envelope::Envelope() {
}

namespace {
constexpr float kInvStageLength = 1.f / Envelope::kStageLength;

/// Level in [0, 1] -> bipolar q31 patch source value, matching the old integer envelope exactly at the ends
[[gnu::always_inline]] inline int32_t toSourceValue(float level) {
	int32_t q = q31_from_float(level); // saturates at 1
	return (q - 1073741824) << 1;
}

[[gnu::always_inline]] inline float stagePosition(uint32_t pos) {
	return static_cast<float>(pos) * kInvStageLength;
}
} // namespace

int32_t Envelope::render(uint32_t numSamples, uint32_t attack, uint32_t decay, uint32_t sustain, uint32_t release,
                         const deluge::modulation::EnvelopeShape& shape) {
	// otherwise modulating sustain with aftertouch causes un smoothed jumps downstream because params don't smooth
	// the envelope value and envelopes are rendered before other params are  calculated (including the envelopes
	// own sustain). To fix this properly we'd first patch and smooth expression to the envelope, then render the
	// envelope, then render everything else, but that seems too complicated right now. This works and doesn't make
	// a huge difference since we'd usually need to smooth it anyway
	auto smoothSustain = [&] {
		float target = q31_to_float(static_cast<int32_t>(sustain));
		float amount = std::min(static_cast<float>(numSamples) * (1.f / 512.f), 1.f);
		smoothedSustain += (target - smoothedSustain) * amount;
	};

considerEnvelopeStage:
	switch (state) {
	case EnvelopeStage::ATTACK:
		pos += attack * numSamples; // Increment the pos *before* taking a value, so we can skip the attack section
		                            // entirely with a high posIncrease
		if (pos >= kStageLength) {
			pos = 0;
			setState(EnvelopeStage::DECAY);
			goto considerEnvelopeStage;
		}
		// Never exactly 0 while attacking
		lastValue = std::max(shape.attack.rising(stagePosition(pos)), 0x1p-31f);
		break;
		// TODO: this would be cool
		//	case EnvelopeStage::HOLD:
		//		lastValue = 1.f;
		//		break;
	case EnvelopeStage::DECAY:
		smoothSustain();
		lastValue = smoothedSustain + shape.decay.falling(stagePosition(pos)) * (1.f - smoothedSustain);

		pos += decay * numSamples;

		if (pos >= kStageLength) {
			setState(EnvelopeStage::SUSTAIN);
		}

		break;

	case EnvelopeStage::SUSTAIN:
		smoothSustain();
		lastValue = smoothedSustain;
		// If sustain is equal to 0, we may as well be switched off already
		if (sustain == 0) {
			setState(EnvelopeStage::OFF);
		}
		else if (ignoredNoteOff) {
			unconditionalRelease();
		}
		break;

	case EnvelopeStage::RELEASE:
		pos += release * numSamples;
		if (pos >= kStageLength) {
			setState(EnvelopeStage::OFF);
			lastValue = 0.f;
			return std::numeric_limits<int32_t>::min();
		}
		lastValue = shape.release.falling(stagePosition(pos)) * lastValuePreCurrentStage;
		break;

	case EnvelopeStage::FAST_RELEASE:
		if (fastReleaseIncrement < 2 * release) {
			release = 2 * release;
			fastReleaseIncrement = release;
		}
		pos += fastReleaseIncrement * numSamples;
		if (pos >= kStageLength) {
			setState(EnvelopeStage::OFF);
			return std::numeric_limits<int32_t>::min();
		}

		// Release in a raised-cosine shape. You'd think it would have less high-frequency content than a linear
		// ramp, and it sounds about the same - though a bit better on Kody's deep bass sample
		lastValue = (0.5f + 0.5f * q31_to_float(getSine(pos + (kStageLength >> 1), 24))) * lastValuePreCurrentStage;
		break;

	default: // OFF
		return std::numeric_limits<int32_t>::min();
	}

	return toSourceValue(lastValue); // Centre the range of the envelope around 0
}

int32_t Envelope::noteOn(bool directlyToDecay, int32_t sustain) {
	smoothedSustain = q31_to_float(sustain);
	ignoredNoteOff = false;
	pos = 0;
	if (!directlyToDecay) {
		setState(EnvelopeStage::ATTACK);
		lastValue = 0.f;
	}
	else {
		setState(EnvelopeStage::DECAY);
		lastValue = 1.f;
	}

	return toSourceValue(lastValue); // Centre the range of the envelope around 0
}

int32_t Envelope::noteOn(uint8_t envelopeIndex, Sound* sound, Voice* voice) {
	int32_t attack = voice->paramFinalValues[params::LOCAL_ENV_0_ATTACK + envelopeIndex];
	int32_t sustain = voice->paramFinalValues[params::LOCAL_ENV_0_SUSTAIN + envelopeIndex];
	bool directlyToDecay = (attack > 245632);

	return noteOn(directlyToDecay, sustain);
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
	if (newState == EnvelopeStage::FAST_RELEASE) {
		if (fastReleaseIncrement < 65536) {
			D_PRINTLN("fast releasing slowly");
		}
	}
}

void Envelope::unconditionalOff() {
	lastValuePreCurrentStage = lastValue;
	setState(EnvelopeStage::OFF);
}

void Envelope::unconditionalRelease(EnvelopeStage typeOfRelease, uint32_t newFastReleaseIncrement) {

	fastReleaseIncrement = newFastReleaseIncrement;

	if (state != typeOfRelease) {
		setState(typeOfRelease);
		pos = 0;
		lastValuePreCurrentStage = lastValue;
	}
}

void Envelope::resumeAttack(float oldLastValue, const deluge::modulation::EnvelopeShape& shape) {
	if (state == EnvelopeStage::ATTACK) {
		pos = static_cast<uint32_t>(shape.attack.risingInverse(oldLastValue) * kStageLength);
	}
}
