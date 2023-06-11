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

#include "Metronome.h"
#include "AudioSample.h"

Metronome::Metronome() {
	sounding = false;
}

void Metronome::trigger(uint32_t newPhaseIncrement) {
	sounding = true;
	phase = 0;
	phaseIncrement = newPhaseIncrement;
	timeSinceTrigger = 0;
}

void Metronome::render(StereoSample* buffer, uint16_t numSamples) {
	if (!sounding) return;

	StereoSample* thisSample = buffer;
	StereoSample* bufferEnd = buffer + numSamples;
	do {

		int32_t value;
		if (phase < 2147483648u) {
			value = 8388608;
		}
		else {
			value = -8388608;
		}

		phase += phaseIncrement;

		thisSample->l += value;
		thisSample->r += value;

	} while (++thisSample != bufferEnd);

	timeSinceTrigger += numSamples;
	if (timeSinceTrigger > 1000) sounding = false;
}
