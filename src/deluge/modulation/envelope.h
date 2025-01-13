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

#pragma once

#include "definitions_cxx.hpp"
#include <cstdint>

class Sound;
class Voice;
class ParamManagerForTimeline;
constexpr uint32_t SOFT_CULL_INCREMENT = 65536;
class Envelope {
public:
	Envelope();

	uint32_t pos;
	EnvelopeStage state; // You may not set this directly, even from this class. Call setState()
	int32_t lastValue;
	int32_t lastValuePreCurrentStage;
	uint32_t timeEnteredState;
	bool ignoredNoteOff;
	uint32_t fastReleaseIncrement{1024};
	int32_t noteOn(bool directlyToDecay);
	int32_t noteOn(uint8_t envelopeIndex, Sound* sound, Voice* voice);
	void noteOff(uint8_t envelopeIndex, Sound* sound, ParamManagerForTimeline* paramManager);
	int32_t render(uint32_t numSamples, uint32_t attack, uint32_t decay, uint32_t sustain, uint32_t release,
	               const uint16_t* releaseTable);
	void unconditionalRelease(EnvelopeStage typeOfRelease = EnvelopeStage::RELEASE,
	                          uint32_t newFastReleaseIncrement = 4096);
	void unconditionalOff();
	void resumeAttack(int32_t oldLastValue);
	void resetTimeEntered();

private:
	void setState(EnvelopeStage newState);
	int32_t smoothedSustain{0};
};
