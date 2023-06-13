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

#include "lfo.h"
#include "functions.h"
#include "lookuptables.h"

LFO::LFO() {
}

int32_t LFO::render(int numSamples, int waveType, uint32_t phaseIncrement) {
	int32_t value;
	switch (waveType) {
	case LFO_TYPE_SAW:
		value = phase;
		break;

	case LFO_TYPE_SQUARE:
		value = getSquare(phase);
		break;

	case LFO_TYPE_SINE:
		value = getSine(phase);
		break;

	case LFO_TYPE_TRIANGLE:
		value = getTriangle(phase);
		break;

	case LFO_TYPE_SAH:
		if (phase == 0) {
			value = CONG;
			holdValue = value;
		}
		else if (phase + phaseIncrement * numSamples < phase) holdValue = CONG;
		else value = holdValue;
		break;

	case LFO_TYPE_RWALK:
        uint32_t range = 4294967295u / 20;
		if (phase == 0) {
			value = holdValue + (range / 2) - CONG % range;
			holdValue = value;
		}
		else if (phase + phaseIncrement * numSamples < phase) holdValue += (range / 2) - CONG % range;
		else value = holdValue;
		break;
	}

	phase += phaseIncrement * numSamples;
	return value;
}

void LFO::tick(int numSamples, uint32_t phaseIncrement) {
	phase += phaseIncrement * numSamples;
}
