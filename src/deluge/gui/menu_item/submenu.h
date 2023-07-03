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

#pragma once

#include "menu_item.h"

namespace menu_item {

class Submenu : public MenuItem {
public:
	Submenu(char const* newName = NULL, MenuItem** newItems = NULL) : MenuItem(newName) { items = newItems; }
	void beginSession(MenuItem* navigatedBackwardFrom = NULL);
	void updateDisplay();
	void selectEncoderAction(int offset) final;
	MenuItem* selectButtonPress() final;
	void readValueAgain() final { updateDisplay(); }
	void unlearnAction() final;
	bool allowsLearnMode() final;
	void learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) final;
	bool learnNoteOn(MIDIDevice* fromDevice, int channel, int noteCode) final;
	void drawPixelsForOled();

	MenuItem** items;
};

} // namespace menu_item
