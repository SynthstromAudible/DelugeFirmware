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
#include "modulation/envelope_curve.h"
#include <cstdint>

class Sound;
class Voice;
class ParamManagerForTimeline;
constexpr uint32_t SOFT_CULL_INCREMENT = 65536;

/// Envelope levels are floats in [0, 1]. Stage positions stay integer: `pos` counts up to kStageLength at the rates
/// the patcher gives us, and an integer accumulator is exact however slow the stage is.
class Envelope {
public:
	static constexpr uint32_t kStageLength = 8388608; // 2^23

	Envelope();

	uint32_t pos;
	EnvelopeStage state; // You may not set this directly, even from this class. Call setState()
	float lastValue;
	float lastValuePreCurrentStage;
	uint32_t timeEnteredState;
	bool ignoredNoteOff;
	uint32_t fastReleaseIncrement{1024};
	/// `sustain` is the level the smoothed sustain starts from
	int32_t noteOn(bool directlyToDecay, int32_t sustain,
	               deluge::modulation::EnvelopeMode mode = deluge::modulation::EnvelopeMode::ADSR);
	int32_t noteOn(uint8_t envelopeIndex, Sound* sound, Voice* voice);
	void noteOff(uint8_t envelopeIndex, Sound* sound, ParamManagerForTimeline* paramManager);
	/// Returns the level as a bipolar q31 patch source value (0 -> INT32_MIN, 1 -> INT32_MAX), or INT32_MIN when off.
	/// `followInput` is what AR mode follows (MPE pressure), as a q31 patch source value; other modes ignore it.
	int32_t render(uint32_t numSamples, uint32_t attack, uint32_t decay, uint32_t sustain, uint32_t release,
	               const deluge::modulation::EnvelopeConfig& config, int32_t followInput = 0);
	void unconditionalRelease(EnvelopeStage typeOfRelease = EnvelopeStage::RELEASE,
	                          uint32_t newFastReleaseIncrement = SOFT_CULL_INCREMENT);
	void unconditionalOff();
	void resumeAttack(float oldLastValue, const deluge::modulation::EnvelopeConfig& config);
	void resetTimeEntered();

private:
	int32_t follow(uint32_t numSamples, uint32_t attack, uint32_t sustain, uint32_t release,
	               const deluge::modulation::EnvelopeShape& shape, int32_t followInput);
	void setState(EnvelopeStage newState);
	float smoothedSustain{0};
};
