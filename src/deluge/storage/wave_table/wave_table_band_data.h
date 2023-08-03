/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

#include "memory/stealable.h"

class WaveTable;

class WaveTableBandData final : public Stealable {
public:
	WaveTableBandData(WaveTable* newWaveTable);

	bool mayBeStolen(void* thingNotToStealFrom = NULL);
	void steal(char const* errorCode);
	int32_t getAppropriateQueue();

	WaveTable* waveTable;
};
