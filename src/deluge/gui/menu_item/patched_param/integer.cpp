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
#include "gui/ui/sound_editor.h"
#include "gui/views/view.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_set.h"

namespace deluge::gui::menu_item::patched_param {
void Integer::readCurrentValue() {
	this->setValue(
	    (((int64_t)soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP()) + 2147483648) * kMaxMenuValue
	     + 2147483648)
	    >> 32);
}

void Integer::writeCurrentValue() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStack = getModelStack(modelStackMemory);
	int32_t value = getFinalValue();
	modelStack->autoParam->setCurrentValueInResponseToUserInput(value, modelStack);

	// send midi follow feedback
	int32_t knobPos = modelStack->paramCollection->paramValueToKnobPos(value, modelStack);
	view.sendMidiFollowFeedback(modelStack, knobPos);

	//((ParamManagerBase*)soundEditor.currentParamManager)->setPatchedParamValue(getP(), getFinalValue(), 0xFFFFFFFF, 0,
	// soundEditor.currentSound, currentSong, getCurrentClip(), true, true);
}

int32_t Integer::getFinalValue() {
	if (this->getValue() == kMaxMenuValue) {
		return 2147483647;
	}
	else if (this->getValue() == kMinMenuValue) {
		return -2147483648;
	}
	else {
		return (uint32_t)this->getValue() * (2147483648 / kMidMenuValue) - 2147483648;
	}
}

} // namespace deluge::gui::menu_item::patched_param
