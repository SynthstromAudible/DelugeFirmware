/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#ifndef CONSEQUENCE_H_
#define CONSEQUENCE_H_

#include "r_typedefs.h"

#define CONSEQUENCE_CLIP_LENGTH 1
#define CONSEQUENCE_CLIP_BEGIN_LINEAR_RECORD 2
#define CONSEQUENCE_PARAM_CHANGE 3
#define CONSEQUENCE_NOTE_ARRAY_CHANGE 4

class InstrumentClip;
class Song;
class ModelStack;

class Consequence {
public:
	Consequence();
	virtual ~Consequence();

	virtual void prepareForDestruction(int whichQueueActionIn, Song* song) {}
	virtual int revert(int time, ModelStack* modelStack) = 0;
	Consequence* next;
	uint8_t type;
};

#endif /* CONSEQUENCE_H_ */
