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

#include <cmath>
#include <cstring>

#include "gui/menu_item/patch_cable_strength/regular.h"
#include "gui/menu_item/source_selection/regular.h"
#include "patched_param.h"
#include "model/clip/instrument_clip.h"
#include "gui/ui/sound_editor.h"
#include "model/song/song.h"
#include "source_selection.h"
#include "patch_cable_strength.h"
#include "hid/display/numeric_driver.h"
#include "hid/matrix/matrix_driver.h"
#include "hid/buttons.h"
#include "model/model_stack.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"

extern "C" {
#include "util/cfunctions.h"
}

namespace menu_item {
MenuItem* PatchedParam::selectButtonPress() {

	// If shift held down, user wants to delete automation
	if (Buttons::isShiftButtonPressed()) {
		return Param::selectButtonPress();
	}
	else {
#if 0 && HAVE_OLED
		return NULL;
#else
		soundEditor.patchingParamSelected = this->getP();
		return &source_selection::regularMenu;
#endif
	}
}

#if !HAVE_OLED
void PatchedParam::drawValue() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(this->getP());
	uint8_t drawDot =
	    soundEditor.currentParamManager->getPatchCableSet()->isAnySourcePatchedToParamVolumeInspecific(paramDescriptor)
	        ? 3
	        : 255;
	numericDriver.setTextAsNumber(soundEditor.currentValue, drawDot);
}
#endif

uint8_t PatchedParam::shouldDrawDotOnName() {
	ParamDescriptor paramDescriptor;
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

uint8_t PatchedParam::getPatchedParamIndex() {
	return this->getP();
}

uint8_t PatchedParam::shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour) {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(this->getP());
	return soundEditor.currentParamManager->getPatchCableSet()->isSourcePatchedToDestinationDescriptorVolumeInspecific(
	           s, paramDescriptor)
	           ? 3
	           : 255;
}

MenuItem* PatchedParam::patchingSourceShortcutPress(int s, bool previousPressStillActive) {
	soundEditor.patchingParamSelected = this->getP();
	source_selection::regularMenu.s = s;
	return &patch_cable_strength::regularMenu;
}

ModelStackWithAutoParam* PatchedParam::getModelStack(void* memory) {
	ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
	ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
	int p = this->getP();
	return modelStack->addParam(summary->paramCollection, summary, p,
	                            &((ParamSet*)summary->paramCollection)->params[p]);
}

} // namespace menu_item
