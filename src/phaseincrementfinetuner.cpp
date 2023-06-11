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

#include "phaseincrementfinetuner.h"
#include "functions.h"

#define TWO_30 1073741824
#define TWO_31 2147483648u

PhaseIncrementFineTuner::PhaseIncrementFineTuner() {
	setNoDetune();
}

void PhaseIncrementFineTuner::setup(int32_t detuneScaled) {
	uint32_t input = TWO_31 + detuneScaled;

	multiplier = interpolateTable(input, 32 /*numBitsInInput*/, centAdjustTableSmall, 0 /*numBitsInTableSize*/);
}

void PhaseIncrementFineTuner::setNoDetune() {
	multiplier = TWO_30;
}

int32_t PhaseIncrementFineTuner::detune(int32_t phaseIncrement) {
	return multiply_32x32_rshift32(phaseIncrement, multiplier) << 2;
}
