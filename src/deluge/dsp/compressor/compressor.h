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

#ifndef COMPRESSOR_H
#define COMPRESSOR_H

#include "definitions.h"
#include "r_typedefs.h"

class Song;

class Compressor {
public:
	Compressor();
	void cloneFrom(Compressor* other);

	uint8_t status;
	uint32_t pos;
	int32_t lastValue;
	int32_t pendingHitStrength;

	int32_t envelopeOffset;
	int32_t envelopeHeight;

	int32_t attack;
	int32_t release;

	SyncType syncType;
	SyncLevel syncLevel; // Basically, 0 is off, max value is 9. Higher numbers are shorter intervals (higher speed).

	int32_t render(uint16_t numSamples, int32_t shapeValue);
	void registerHit(int32_t strength);
	void registerHitRetrospectively(int32_t strength, uint32_t numSamplesAgo);

private:
	int32_t getActualAttackRate();
	int32_t getActualReleaseRate();
};

#endif // COMPRESSOR_H
