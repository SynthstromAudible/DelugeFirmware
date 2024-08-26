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
#include "io/debug/log.h"
#include "model/sync.h"
#include "util/fixedpoint.h"
#include "util/waves.h"

class LFOConfig {
public:
	LFOConfig() : waveType(LFOType::TRIANGLE), syncType(SYNC_TYPE_EVEN), syncLevel(SYNC_LEVEL_NONE) {}
	LFOConfig(LFOType type) : waveType(type), syncType(SYNC_TYPE_EVEN), syncLevel(SYNC_LEVEL_NONE) {}
	LFOType waveType;
	SyncType syncType;
	SyncLevel syncLevel;
};

class LFO {
public:
	LFO() = default;
	uint32_t phase{0};
	int32_t holdValue{0};
	int32_t target{0};
	int32_t speed{0};
	void setLocalInitialPhase(const LFOConfig& config);
	void setGlobalInitialPhase(const LFOConfig& config);
	[[gnu::always_inline]] int32_t render(int32_t numSamples, LFOConfig& config, uint32_t phaseIncrement) {
		return render(numSamples, config.waveType, phaseIncrement);
	}
	[[gnu::always_inline]] int32_t render(int32_t numSamples, LFOType waveType, uint32_t phaseIncrement) {
		int32_t value;
		switch (waveType) {
		case LFOType::SAW:
			value = static_cast<int32_t>(phase);
			break;

		case LFOType::SQUARE:
			value = getSquare(phase);
			break;

		case LFOType::SINE:
			value = getSine(phase);
			break;

		case LFOType::TRIANGLE:
			value = getTriangle(phase);
			break;

		case LFOType::SAMPLE_AND_HOLD:
			if ((phase == 0) || (phase + phaseIncrement * numSamples < phase)) {
				value = CONG;
				holdValue = value;
			}
			else {
				value = holdValue;
			}
			break;

		case LFOType::RANDOM_WALK: {
			uint32_t range = 4294967295u / 20;
			if (phase == 0) {
				value = (range / 2) - CONG % range;
				holdValue = value;
			}
			else if (phase + phaseIncrement * numSamples < phase) {
				// (holdValue / -16) adds a slight bias to make the new value move
				// back towards zero modulation the further holdValue has moved away
				// from zero. This is probably best explained by showing the edge
				// cases:
				// holdValue == 8 * range => (holdValue / -16) + (range / 2) == 0 =>
				// next holdValue <= current holdValue
				// holdValue == 0 => (holdValue / -16) + (range / 2) == range / 2 =>
				// equal chance for next holdValue smaller or lager than current
				// holdValue == -8 * range => (holdValue / -16) + (range / 2) ==
				// range => next holdValue >= current holdValuie
				holdValue = add_saturation((holdValue / -16) + (range / 2) - CONG % range, holdValue);
				value = holdValue;
			}
			else {
				value = holdValue;
			}
			break;
		}
		case LFOType::WARBLER: {
			phaseIncrement *= 2;
			warble(numSamples, phaseIncrement);
			value = holdValue;
		}
		}

		phase += phaseIncrement * numSamples;
		return value;
	}
	void warble(int32_t numSamples, uint32_t phaseIncrement) {
		if (phase + phaseIncrement * numSamples < phase) {
			target = CONG;
			speed = 0;
		}
		// this is a second order filter - the rate that the held value approaches the target is filtered instead of
		// the difference. It makes a nice smooth random curve since the derivative is 0 at the start and end
		auto targetSpeed = target - holdValue;
		speed = speed + numSamples * (multiply_32x32_rshift32(targetSpeed, phaseIncrement >> 8));
		holdValue = add_saturation(holdValue, speed);
	}

	void tick(int32_t numSamples, uint32_t phaseIncrement) {
		// Note: if this overflows and we're using S&H or RANDOM_WALK, the
		// next render() won't know it has overflown. Probably not an issue.
		phase += phaseIncrement * numSamples;
	}
};
