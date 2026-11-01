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

#include "model/consequence/consequence.h"
#include "model/model_stack.h"
#include "processing/sound/mod_matrix_host.h"

/// @brief The undo record of one cable topology edit on one clip: reverting applies the edit through the clip's mod
/// matrix in either direction.
class ConsequenceInstrumentDelta final : public Consequence {
public:
	ConsequenceInstrumentDelta(ModelStackWithThreeMainThings const* modelStack, CableEdit edit);
	Error revert(TimeType time, ModelStack* modelStack) override;

private:
	// The target as ConsequenceParamChange captures its own: a copy of the stack's sound and param manager.
	union {
		alignas(ModelStackWithAutoParam) char modelStackMemory_[MODEL_STACK_MAX_SIZE]{};
		ModelStackWithThreeMainThings target_;
	};
	CableEdit edit_;
};
