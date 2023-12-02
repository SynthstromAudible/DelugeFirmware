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

#pragma once

#include <cmath>
#include <cstdint>

class StereoSample;

class Metronome {
public:
	Metronome();
	void trigger(uint32_t newPhaseIncrement);
	void render(StereoSample* buffer, uint16_t numSamples);
	void setVolume(int32_t linearParam) { metronomeVolume = (exp(float(linearParam) / 200.0f) - 1.0) * float(1 << 27); }

	uint32_t phase;
	uint32_t phaseIncrement;
	uint32_t timeSinceTrigger;
	uint32_t metronomeVolume;
	bool sounding;
};
