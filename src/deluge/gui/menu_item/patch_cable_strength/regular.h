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
#pragma once
#include "definitions_cxx.hpp"
#include "gui/menu_item/patch_cable_strength.h"

namespace deluge::gui::menu_item::patch_cable_strength {

class Regular : public PatchCableStrength {
public:
	using PatchCableStrength::PatchCableStrength;

	ParamDescriptor getDestinationDescriptor() final;
	PatchSource getS() final;
	ParamDescriptor getLearningThing() final;
	MenuPermission checkPermissionToBeginSession(Sound* sound, int whichThing, MultiRange** currentRange) override;
	uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) override;
	MenuItem* patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive) override;
	MenuItem* selectButtonPress() final;
};

extern Regular regularMenu;
} // namespace deluge::gui::menu_item::patch_cable_strength
