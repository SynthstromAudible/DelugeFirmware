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
#include "gui/menu_item/value_scaling.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item {

void UnpatchedParam::readCurrentValue() {
	this->setValue(computeCurrentValueForStandardMenuItem(
	    soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP())));
}

ModelStackWithAutoParam* UnpatchedParam::getModelStack(void* memory) {
	ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
	return modelStack->getUnpatchedAutoParamFromId(getP());
}

void UnpatchedParam::writeCurrentValue() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStackWithParam = getModelStack(modelStackMemory);
	int32_t value = getFinalValue();

	// If affect-entire button held, do whole kit
	if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

		Kit* kit = getCurrentKit();

		for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
			if (thisDrum->type == DrumType::SOUND) {
				auto* soundDrum = static_cast<SoundDrum*>(thisDrum);

				char modelStackMemoryForSoundDrum[MODEL_STACK_MAX_SIZE];
				ModelStackWithAutoParam* modelStackForSoundDrum =
				    getModelStackFromSoundDrum(modelStackMemoryForSoundDrum, soundDrum)
				        ->getUnpatchedAutoParamFromId(getP());
				modelStackForSoundDrum->autoParam->setCurrentValueInResponseToUserInput(value, modelStackForSoundDrum);
			}
		}
	}
	// Or, the normal case of just one sound
	else {
		modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(value, modelStackWithParam);
	}

	// send midi follow feedback
	int32_t knobPos = modelStackWithParam->paramCollection->paramValueToKnobPos(value, modelStackWithParam);
	view.sendMidiFollowFeedback(modelStackWithParam, knobPos);

	if (getRootUI() == &automationView) {
		int32_t p = modelStackWithParam->paramId;
		modulation::params::Kind kind = modelStackWithParam->paramCollection->getParamKind();
		automationView.possiblyRefreshAutomationEditorGrid(getCurrentClip(), kind, p);
	}
}

int32_t UnpatchedParam::getFinalValue() {
	return computeFinalValueForStandardMenuItem(this->getValue());
}

ParamDescriptor UnpatchedParam::getLearningThing() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(getP() + deluge::modulation::params::UNPATCHED_START);
	return paramDescriptor;
}

ParamSet* UnpatchedParam::getParamSet() {
	return soundEditor.currentParamManager->getUnpatchedParamSet();
}

deluge::modulation::params::Kind UnpatchedParam::getParamKind() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	return getModelStack(modelStackMemory)->paramCollection->getParamKind();
}

uint32_t UnpatchedParam::getParamIndex() {
	return this->getP();
}

// ---------------------------------------

// ---------------------------------------

} // namespace deluge::gui::menu_item
