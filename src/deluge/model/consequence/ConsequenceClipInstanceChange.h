/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#ifndef CONSEQUENCECLIPINSTANCECHANGE_H_
#define CONSEQUENCECLIPINSTANCECHANGE_H_
#include "Consequence.h"
#include "r_typedefs.h"

class Output;
class Clip;
class ClipInstance;

class ConsequenceClipInstanceChange final : public Consequence {
public:
	ConsequenceClipInstanceChange(Output* newOutput, ClipInstance* clipInstance, int32_t posAfter, int32_t lengthAfter,
	                              Clip* clipAfter);
	int revert(int time, ModelStack* modelStack);

	Output* output;
	int32_t pos[2];
	int32_t length[2];
	Clip* clip[2];
};

#endif /* CONSEQUENCECLIPINSTANCECHANGE_H_ */
