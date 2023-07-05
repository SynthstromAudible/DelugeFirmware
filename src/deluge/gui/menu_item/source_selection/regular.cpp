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
#include "gui/menu_item/patch_cable_strength/regular.h"
#include "modulation/params/param_descriptor.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::source_selection {
Regular regularMenu{};

Regular::Regular() {
#if HAVE_OLED
	basicTitle = "Modulate with";
#endif
}

ParamDescriptor Regular::getDestinationDescriptor() {
	ParamDescriptor descriptor;
	descriptor.setToHaveParamOnly(soundEditor.patchingParamSelected);
	return descriptor;
}

MenuItem* Regular::selectButtonPress() {
	return &patch_cable_strength::regularMenu;
}

void Regular::beginSession(MenuItem* navigatedBackwardFrom) {

	if (navigatedBackwardFrom) {
		if (soundEditor.patchingParamSelected == PARAM_GLOBAL_VOLUME_POST_REVERB_SEND
		    || soundEditor.patchingParamSelected == PARAM_LOCAL_VOLUME) {
			soundEditor.patchingParamSelected = PARAM_GLOBAL_VOLUME_POST_FX;
		}
	}

	SourceSelection::beginSession(navigatedBackwardFrom);
}

MenuItem* Regular::patchingSourceShortcutPress(int newS, bool previousPressStillActive) {
	s = newS;
	return &regularMenu;
}

} // namespace menu_item::source_selection
