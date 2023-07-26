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

#include "RZA1/system/r_typedefs.h"

#include "gui/positionable.h"

class Note : public Positionable {
public:
	Note();

	inline void setLength(int newLength) { length = newLength; }

	inline int getLength() { return length; }

	inline void setVelocity(int newVelocity) { velocity = newVelocity; }

	inline int getVelocity() { return velocity; }

	inline void setProbability(int newProbability) { probability = newProbability; }

	inline int getProbability() { return probability; }

	inline void setLift(int newLift) { lift = newLift; }

	inline int getLift() { return lift; }

	inline void setAccidentalTranspose(int newAccidentalTranspose) { accidentalTranspose = newAccidentalTranspose; }

	inline int getAccidentalTranspose() { return accidentalTranspose; }


	void writeToFile();

	int32_t length;
	uint8_t velocity;
	uint8_t probability;
	uint8_t lift;
	int8_t accidentalTranspose;

	// Understanding the probability field: the first bit says whether it's based on a previous one.
	// So take the rightmost 7 bits. If that's greater than NUM_PROBABILITY_VALUES (20),
};
