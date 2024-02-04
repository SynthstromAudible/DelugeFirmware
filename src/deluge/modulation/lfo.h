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
#include "util/functions.h"

class LFO {
public:
	LFO() = default;
	uint32_t phase;
	int32_t holdValue;
	[[gnu::always_inline]] int32_t render(int32_t numSamples, LFOType waveType, uint32_t phaseIncrement) {
		int32_t value;
		switch (waveType) {
		case LFOType::SAW:
			value = phase;
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
			if (phase == 0) {
				value = CONG;
				holdValue = value;
			}
			else if (phase + phaseIncrement * numSamples < phase) {
				holdValue = CONG;
			}
			else {
				value = holdValue;
			}
			break;

		case LFOType::RANDOM_WALK:
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
				holdValue += (holdValue / -16) + (range / 2) - CONG % range;
			}
			else {
				value = holdValue;
			}
			break;
		}

		phase += phaseIncrement * numSamples;
		return value;
	}

	void tick(int32_t numSamples, uint32_t phaseIncrement) { phase += phaseIncrement * numSamples; }
};
