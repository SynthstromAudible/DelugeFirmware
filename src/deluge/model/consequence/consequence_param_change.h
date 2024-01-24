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

#pragma once

#include "model/consequence/consequence.h"
#include "model/model_stack.h"
#include "modulation/automation/auto_param.h"
#include <cstdint>

class ConsequenceParamChange final : public Consequence {
public:
	ConsequenceParamChange(ModelStackWithAutoParam const* modelStack, bool stealData);
	int32_t revert(TimeType time, ModelStack* modelStackWithSong);

	union {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithParamId modelStack; // TODO: yikes, is this safe? What about NoteRow pointers etc?
	};
	AutoParamState state;
};
