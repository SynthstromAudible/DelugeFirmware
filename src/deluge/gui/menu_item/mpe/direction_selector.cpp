/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

#include "direction_selector.h"
#include "gui/ui/sound_editor.h"
#include "zone_selector.h"
#include "io/midi/midi_device.h"

namespace deluge::gui::menu_item::mpe {

DirectionSelector directionSelectorMenu{"MPE"};

void DirectionSelector::beginSession(MenuItem* navigatedBackwardFrom) {
	if (!navigatedBackwardFrom) {
		whichDirection = MIDI_DIRECTION_INPUT_TO_DELUGE;
	}
	Selection::beginSession(navigatedBackwardFrom);
}

Sized<char const**> DirectionSelector::getOptions() {
	static char const* options[] = {"In", "Out"};
	return {options, 2};
}

void DirectionSelector::readCurrentValue() {
	this->value_ = whichDirection;
}

void DirectionSelector::writeCurrentValue() {
	whichDirection = this->value_;
}

MenuItem* DirectionSelector::selectButtonPress() {
	return &zoneSelectorMenu;
}
} // namespace deluge::gui::menu_item::mpe
