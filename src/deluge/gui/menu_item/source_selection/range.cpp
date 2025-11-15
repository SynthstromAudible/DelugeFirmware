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
#include "range.h"
#include "gui/menu_item/patch_cable_strength/range.h"
#include "gui/ui/sound_editor.h"
#include "modulation/params/param_descriptor.h"
#include "regular.h"

namespace deluge::gui::menu_item::source_selection {
PLACE_SDRAM_BSS Range rangeMenu{};

ParamDescriptor Range::getDestinationDescriptor() {
	ParamDescriptor descriptor{};
	descriptor.setToHaveParamAndSource(soundEditor.patchingParamSelected, regularMenu.s);
	return descriptor;
}

MenuItem* Range::selectButtonPress() {
	return &patch_cable_strength::rangeMenu;
}

MenuItem* Range::patchingSourceShortcutPress(PatchSource newS, bool previousPressStillActive) {
	return NO_NAVIGATION;
}

} // namespace deluge::gui::menu_item::source_selection
