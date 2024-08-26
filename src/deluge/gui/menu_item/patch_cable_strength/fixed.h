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
#include "regular.h"

namespace deluge::gui::menu_item::patch_cable_strength {

class Fixed : public Regular {
public:
	Fixed(l10n::String newName, int32_t newP = 0, PatchSource newS = PatchSource::LFO_GLOBAL_1)
	    : Regular(newName), p(newP), s(newS) {}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             MultiRange** currentRange) final;
	uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) final;
	MenuItem* patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive) final;

protected:
	uint8_t p;
	PatchSource s;
};
} // namespace deluge::gui::menu_item::patch_cable_strength
