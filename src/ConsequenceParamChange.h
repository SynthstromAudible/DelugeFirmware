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

#ifndef CONSEQUENCEPARAMAUTOMATIONRECORD_H_
#define CONSEQUENCEPARAMAUTOMATIONRECORD_H_

#include "r_typedefs.h"
#include "AutoParam.h"
#include "Consequence.h"
#include "ModelStack.h"

class ConsequenceParamChange final : public Consequence {
public:
	ConsequenceParamChange(ModelStackWithAutoParam const* modelStack, bool stealData);
	int revert(int time, ModelStack* modelStackWithSong);

	union {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithParamId modelStack; // TODO: yikes, is this safe? What about NoteRow pointers etc?
	};
	AutoParamState state;
};

#endif /* CONSEQUENCEPARAMAUTOMATIONRECORD_H_ */
