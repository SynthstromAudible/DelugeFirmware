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

#ifndef MENUITEMMIDIDEVICES_H_
#define MENUITEMMIDIDEVICES_H_

#include "MenuItem.h"

class MIDIDevice;

class MenuItemMIDIDevices final : public MenuItem {
public:
	MenuItemMIDIDevices(char const* newName = 0) : MenuItem(newName) {}
	void beginSession(MenuItem* navigatedBackwardFrom = NULL);
	void selectEncoderAction(int offset);
	MIDIDevice* getDevice(int deviceIndex);
	void drawValue();
	MenuItem* selectButtonPress();
	void drawPixelsForOled();
};

extern MenuItemMIDIDevices midiDevicesMenu;

#endif /* MENUITEMMIDIDEVICES_H_ */
