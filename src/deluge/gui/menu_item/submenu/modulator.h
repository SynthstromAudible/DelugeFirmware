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
#include "gui/menu_item/submenu_referring_to_one_thing.h"
#include "processing/sound/sound.h"

extern void setModulatorNumberForTitles(int32_t);

namespace deluge::gui::menu_item::submenu {
template <size_t n>
class Modulator final : public SubmenuReferringToOneThing<n> {
public:
	using SubmenuReferringToOneThing<n>::SubmenuReferringToOneThing;

	// OLED Only
	void beginSession(MenuItem* navigatedBackwardFrom) {
		setModulatorNumberForTitles(this->thingIndex);
		SubmenuReferringToOneThing<n>::beginSession(navigatedBackwardFrom);
	}

	bool isRelevant(Sound* sound, int32_t whichThing) { return (sound->synthMode == SynthMode::FM); }
};

// Template deduction guide, will not be required with P2582@C++23
template <size_t n>
Modulator(const string&, MenuItem* const (&)[n], int32_t) -> Modulator<n>;

} // namespace deluge::gui::menu_item::submenu
