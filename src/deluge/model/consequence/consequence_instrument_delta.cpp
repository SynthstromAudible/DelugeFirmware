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
#include "model/consequence/consequence_instrument_delta.h"

#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

#include <utility>

ConsequenceInstrumentDelta::ConsequenceInstrumentDelta(deluge::project::ParamAddress owner, CableEdit edit)
    : owner_(owner), edit_(std::move(edit)) {
}

Error ConsequenceInstrumentDelta::revert(TimeType time, ModelStack* modelStack) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	if (ModelStackWithThreeMainThings* sound = deluge::bridge::soundStackExact(*modelStack->song, owner_, memory)) {
		ModMatrixHost matrix{*static_cast<Sound*>(sound->modControllable), *sound->paramManager};
		matrix.apply(edit_, time);
	}
	return Error::NONE;
}
