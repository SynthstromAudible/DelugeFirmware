/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "integer.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_set.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::patched_param {
void Integer::readCurrentValue() {
	soundEditor.currentValue =
	    (((int64_t)soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP()) + 2147483648) * 50
	     + 2147483648)
	    >> 32;
}

void Integer::writeCurrentValue() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStack = getModelStack(modelStackMemory);
	modelStack->autoParam->setCurrentValueInResponseToUserInput(getFinalValue(), modelStack);

	//((ParamManagerBase*)soundEditor.currentParamManager)->setPatchedParamValue(getP(), getFinalValue(), 0xFFFFFFFF, 0, soundEditor.currentSound, currentSong, currentSong->currentClip, true, true);
}

int32_t Integer::getFinalValue() {
	if (soundEditor.currentValue == 25) {
		return 0;
	}
	return (uint32_t)soundEditor.currentValue * 85899345 - 2147483648;
}

} // namespace menu_item::patched_param
