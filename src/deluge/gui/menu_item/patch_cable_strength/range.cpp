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
#include "gui/menu_item/source_selection/range.h"
#include "gui/menu_item/source_selection/regular.h"
#include "gui/ui/sound_editor.h"

namespace deluge::gui::menu_item::patch_cable_strength {
PLACE_SDRAM_BSS Range rangeMenu{};

ParamDescriptor Range::getLearningThing() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamAndTwoSources(soundEditor.patchingParamSelected, source_selection::regularMenu.s,
	                                            source_selection::rangeMenu.s);
	return paramDescriptor;
}

ParamDescriptor Range::getDestinationDescriptor() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamAndSource(soundEditor.patchingParamSelected, source_selection::regularMenu.s);
	return paramDescriptor;
}

PatchSource Range::getS() {
	return source_selection::rangeMenu.s;
}

uint8_t Range::shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) {

	// If this is the actual source we're editing for...
	if (s == getS()) {
		*colour = 0b110;
		return 0;
	}

	// Or, if it's the source whose range we are controlling...
	if (source_selection::regularMenu.s == s) {
		return 3; // Did I get this right? #patchingoverhaul2021
	}

	return 255;
}

MenuItem* Range::patchingSourceShortcutPress(PatchSource newS, bool previousPressStillActive) {
	return NO_NAVIGATION;
}

// FixedPatchCableStrength ----------------------------------------------------------------------------

} // namespace deluge::gui::menu_item::patch_cable_strength
