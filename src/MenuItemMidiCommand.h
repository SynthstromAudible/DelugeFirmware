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

#ifndef MENUITEMMIDICOMMAND_H_
#define MENUITEMMIDICOMMAND_H_

#include "MenuItem.h"

class MIDIDevice;

class MenuItemMidiCommand final : public MenuItem {
public:
	MenuItemMidiCommand(char const* newName = NULL, int newCommandNumber = 0) : MenuItem(newName) {
		commandNumber = newCommandNumber;
	}
	void beginSession(MenuItem* navigatedBackwardFrom);
	void drawValue();
	void selectEncoderAction(int offset);
	bool allowsLearnMode() { return true; }
	bool shouldBlinkLearnLed() { return true; }
	void unlearnAction();
	bool learnNoteOn(MIDIDevice* device, int channel, int noteCode);
	void learnCC(MIDIDevice* device, int channel, int ccNumber, int value);
#if HAVE_OLED
	void drawPixelsForOled();
#endif

	uint8_t commandNumber;
};

#endif /* MENUITEMMIDICOMMAND_H_ */
