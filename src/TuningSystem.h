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

#ifndef TUNINGSYSTEM_H_
#define TUNINGSYSTEM_H_

#include "r_typedefs.h"

#define NUM_TUNING_BANKS 1

extern int32_t tuningFrequencyTable[12];
extern int32_t tuningIntervalTable[12];
extern int32_t selectedTuningBank;

class TuningSystem
{
public:
	TuningSystem();
	void setDefaultTuning();
	void setOffset(int,int32_t);
	void setBank(int);

	double referenceFrequency;
	int currentNote;
	int currentValue;
	int32_t offsets[12]; // cents -5000..+5000

	int32_t noteInterval(int);
	int32_t noteFrequency(int);
private:
	void calculateOffset(int);
	void calculateUserTuning();
};

extern TuningSystem tuningSystem;

#endif
