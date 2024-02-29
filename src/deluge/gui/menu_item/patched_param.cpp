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

#include "patched_param.h"
#include "gui/menu_item/patch_cable_strength/regular.h"
#include "gui/menu_item/source_selection/regular.h"
#include "gui/ui/sound_editor.h"
#include "hid/buttons.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "source_selection.h"

namespace deluge::gui::menu_item {

MenuItem* PatchedParam::selectButtonPress() {

	// If shift held down, user wants to delete automation
	if (Buttons::isShiftButtonPressed()) {
		return Param::selectButtonPress();
	}
	soundEditor.patchingParamSelected = this->getP();
	return &source_selection::regularMenu;
}

uint8_t PatchedParam::shouldDrawDotOnName() {
	ParamDescriptor paramDescriptor{};
	paramDescriptor.setToHaveParamOnly(this->getP());
	return soundEditor.currentParamManager->getPatchCableSet()->isAnySourcePatchedToParamVolumeInspecific(
	           paramDescriptor)
	           ? 3
	           : 255;
}

ParamDescriptor PatchedParam::getLearningThing() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(this->getP());
	return paramDescriptor;
}

ParamSet* PatchedParam::getParamSet() {
	return soundEditor.currentParamManager->getPatchedParamSet();
}

deluge::modulation::params::Kind PatchedParam::getParamKind() {
	return deluge::modulation::params::Kind::PATCHED;
}

uint32_t PatchedParam::getParamIndex() {
	return this->getP();
}

uint8_t PatchedParam::shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) {
	ParamDescriptor paramDescriptor{};
	paramDescriptor.setToHaveParamOnly(this->getP());
	return soundEditor.currentParamManager->getPatchCableSet()->isSourcePatchedToDestinationDescriptorVolumeInspecific(
	           s, paramDescriptor)
	           ? 3
	           : 255;
}

MenuItem* PatchedParam::patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive) {
	soundEditor.patchingParamSelected = this->getP();
	source_selection::regularMenu.s = s;
	return &patch_cable_strength::regularMenu;
}

ModelStackWithAutoParam* PatchedParam::getModelStack(void* memory) {
	ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
	ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
	int32_t p = this->getP();
	return modelStack->addParam(summary->paramCollection, summary, p,
	                            &(static_cast<ParamSet*>(summary->paramCollection))->params[p]);
}

} // namespace deluge::gui::menu_item
