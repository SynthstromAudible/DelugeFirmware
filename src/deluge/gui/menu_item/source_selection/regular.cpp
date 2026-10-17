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
#include "gui/l10n/l10n.h"
#include "gui/menu_item/patch_cable_strength/regular.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "modulation/params/param_descriptor.h"

namespace deluge::gui::menu_item::source_selection {
Regular regularMenu{};

ParamDescriptor Regular::getDestinationDescriptor() {
	ParamDescriptor descriptor{};
	descriptor.setToHaveParamOnly(soundEditor.patchingParamSelected);
	return descriptor;
}

MenuItem* Regular::selectButtonPress() {
	return &patch_cable_strength::regularMenu;
}

void Regular::beginSession(MenuItem* navigatedBackwardFrom) {

	if (navigatedBackwardFrom != nullptr) {
		if (soundEditor.patchingParamSelected == deluge::modulation::params::GLOBAL_VOLUME_POST_REVERB_SEND
		    || soundEditor.patchingParamSelected == deluge::modulation::params::LOCAL_VOLUME) {
			soundEditor.patchingParamSelected = deluge::modulation::params::GLOBAL_VOLUME_POST_FX;
		}
	}

	SourceSelection::beginSession(navigatedBackwardFrom);
}

MenuItem* Regular::patchingSourceShortcutPress(PatchSource newS, [[maybe_unused]] bool previous_press_still_active) {
	const auto previous_param = soundEditor.patchingParamSelected;
	if (previous_param == deluge::modulation::params::GLOBAL_VOLUME_POST_REVERB_SEND
	    || previous_param == deluge::modulation::params::LOCAL_VOLUME) {
		soundEditor.patchingParamSelected = deluge::modulation::params::GLOBAL_VOLUME_POST_FX;
	}

	// Validate the logical destination before rerouting it for the new source.
	if (!sourceIsAllowed(newS)) {
		soundEditor.patchingParamSelected = previous_param;
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_PARAMETER_NOT_APPLICABLE));
		return nullptr;
	}

	s = newS;
	return &patch_cable_strength::regularMenu;
}

} // namespace deluge::gui::menu_item::source_selection
