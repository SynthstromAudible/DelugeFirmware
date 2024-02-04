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

#include "unpatched_param.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/view.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"

namespace deluge::gui::menu_item {

void UnpatchedParam::readCurrentValue() {
	this->setValue((((int64_t)soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP()) + 2147483648)
	                    * kMaxMenuValue
	                + 2147483648)
	               >> 32);
}

ModelStackWithAutoParam* UnpatchedParam::getModelStack(void* memory) {
	ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
	ParamCollectionSummary* summary = modelStack->paramManager->getUnpatchedParamSetSummary();
	int32_t p = getP();
	return modelStack->addParam(summary->paramCollection, summary, p,
	                            &((ParamSet*)summary->paramCollection)->params[p]);
}

void UnpatchedParam::writeCurrentValue() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStackWithParam = getModelStack(modelStackMemory);
	int32_t value = getFinalValue();
	modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(value, modelStackWithParam);

	// send midi follow feedback
	int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(value, modelStackWithParam);
	view.sendMidiFollowFeedback(modelStackWithParam, knobPos);
}

int32_t UnpatchedParam::getFinalValue() {
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

ParamDescriptor UnpatchedParam::getLearningThing() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(getP() + deluge::modulation::params::UNPATCHED_START);
	return paramDescriptor;
}

ParamSet* UnpatchedParam::getParamSet() {
	return soundEditor.currentParamManager->getUnpatchedParamSet();
}

// ---------------------------------------

// ---------------------------------------

} // namespace deluge::gui::menu_item
