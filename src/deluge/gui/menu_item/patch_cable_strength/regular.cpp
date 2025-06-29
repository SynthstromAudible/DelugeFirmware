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
#include "regular.h"
#include "definitions_cxx.hpp"
#include "gui/menu_item/patch_cable_strength/range.h"
#include "gui/menu_item/source_selection/range.h"
#include "gui/menu_item/source_selection/regular.h"
#include "gui/ui/sound_editor.h"
#include "hid/buttons.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::patch_cable_strength {
Regular regularMenu{};

ParamDescriptor Regular::getLearningThing() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamAndSource(soundEditor.patchingParamSelected, source_selection::regularMenu.s);
	return paramDescriptor;
}

ParamDescriptor Regular::getDestinationDescriptor() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(soundEditor.patchingParamSelected);
	return paramDescriptor;
}

PatchSource Regular::getS() {
	return source_selection::regularMenu.s;
}

MenuPermission Regular::checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
                                                      MultiRange** currentRange) {

	Sound* sound = static_cast<Sound*>(modControllable);

	if (soundEditor.patchingParamSelected == deluge::modulation::params::GLOBAL_VOLUME_POST_FX) {
		if (sound->maySourcePatchToParam(getS(), soundEditor.patchingParamSelected,
		                                 ((ParamManagerForTimeline*)soundEditor.currentParamManager))
		    == PatchCableAcceptance::DISALLOWED) {
			soundEditor.patchingParamSelected = deluge::modulation::params::GLOBAL_VOLUME_POST_REVERB_SEND;
			if (sound->maySourcePatchToParam(getS(), soundEditor.patchingParamSelected,
			                                 ((ParamManagerForTimeline*)soundEditor.currentParamManager))
			    == PatchCableAcceptance::DISALLOWED) {
				soundEditor.patchingParamSelected = deluge::modulation::params::LOCAL_VOLUME;
			}
		}
	}

	return PatchCableStrength::checkPermissionToBeginSession(modControllable, whichThing, currentRange);
}

uint8_t Regular::shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) {

	// If this is the actual source we're editing for...
	if (s == getS()) {
		return 0;
	}

	PatchCableSet* patchCableSet = soundEditor.currentParamManager->getPatchCableSet();

	// Or, if it's the source controlling the range of the source we're editing for...
	if (patchCableSet->getPatchCableIndex(s, getLearningThing()) != 255) {
		*colour = 0b110;
		return 3;
	}

	return 255;
}

MenuItem* Regular::patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive) {
	if (previousPressStillActive) {
		source_selection::rangeMenu.s = s;
		return &patch_cable_strength::rangeMenu;
	}
	return NO_NAVIGATION;
}

} // namespace deluge::gui::menu_item::patch_cable_strength
