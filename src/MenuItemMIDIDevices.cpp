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

#include <MenuItemMIDIDevices.h>
#include "soundeditor.h"
#include "MIDIDeviceManager.h"
#include "numericdriver.h"
#include "MIDIDevice.h"
#include "MenuItemSubmenu.h"

MenuItemMIDIDevices midiDevicesMenu;

extern MenuItemSubmenu midiDeviceMenu;

void MenuItemMIDIDevices::beginSession(MenuItem* navigatedBackwardFrom) {
	if (navigatedBackwardFrom) {
		for (soundEditor.currentValue = -2;
		     soundEditor.currentValue < MIDIDeviceManager::hostedMIDIDevices.getNumElements();
		     soundEditor.currentValue++) {
			if (getDevice(soundEditor.currentValue) == soundEditor.currentMIDIDevice) goto decidedDevice;
		}
	}

	soundEditor.currentValue = -2; // Start on "DIN". That's the only one that'll always be there.

decidedDevice:
	soundEditor.currentMIDIDevice = getDevice(soundEditor.currentValue);
#if HAVE_OLED
	soundEditor.menuCurrentScroll = soundEditor.currentValue;
#else
	drawValue();
#endif
}

void MenuItemMIDIDevices::selectEncoderAction(int offset) {
	do {
		int newValue = soundEditor.currentValue + offset;

		if (newValue >= MIDIDeviceManager::hostedMIDIDevices.getNumElements()) {
			if (HAVE_OLED) return;
			newValue = -2;
		}
		else if (newValue < -2) {
			if (HAVE_OLED) return;
			newValue = MIDIDeviceManager::hostedMIDIDevices.getNumElements() - 1;
		}

		soundEditor.currentValue = newValue;

		soundEditor.currentMIDIDevice = getDevice(soundEditor.currentValue);

	} while (!soundEditor.currentMIDIDevice->connectionFlags);
	// Don't show devices which aren't connected. Sometimes we won't even have a name to display for them.

#if HAVE_OLED
	if (soundEditor.currentValue < soundEditor.menuCurrentScroll)
		soundEditor.menuCurrentScroll = soundEditor.currentValue;

	if (offset >= 0) {
		int d = soundEditor.currentValue;
		int numSeen = 1;
		while (true) {
			d--;
			if (d == soundEditor.menuCurrentScroll) break;
			if (!getDevice(d)->connectionFlags) continue;
			numSeen++;
			if (numSeen >= OLED_MENU_NUM_OPTIONS_VISIBLE) {
				soundEditor.menuCurrentScroll = d;
				break;
			}
		}
	}
#endif

	drawValue();
}

MIDIDevice* MenuItemMIDIDevices::getDevice(int deviceIndex) {
	if (deviceIndex == -2) {
		return &MIDIDeviceManager::dinMIDIPorts;
	}
	else if (deviceIndex == -1) {
		return &MIDIDeviceManager::upstreamUSBMIDIDevice;
	}
	else {
		return (MIDIDevice*)MIDIDeviceManager::hostedMIDIDevices.getElement(deviceIndex);
	}
}

void MenuItemMIDIDevices::drawValue() {
#if HAVE_OLED
	renderUIsForOled();
#else
	char const* displayName = soundEditor.currentMIDIDevice->getDisplayName();
	numericDriver.setScrollingText(displayName);
#endif
}

MenuItem* MenuItemMIDIDevices::selectButtonPress() {
#if HAVE_OLED
	midiDeviceMenu.basicTitle =
	    soundEditor.currentMIDIDevice->getDisplayName(); // A bit ugly, but saves us extending a class.
#endif
	return &midiDeviceMenu;
}

#if HAVE_OLED

void MenuItemMIDIDevices::drawPixelsForOled() {
	char const* itemNames[OLED_MENU_NUM_OPTIONS_VISIBLE];

	int selectedRow = -1;

	int d = soundEditor.menuCurrentScroll;
	int r = 0;
	while (r < OLED_MENU_NUM_OPTIONS_VISIBLE && d < MIDIDeviceManager::hostedMIDIDevices.getNumElements()) {
		MIDIDevice* device = getDevice(d);
		if (device->connectionFlags) {
			itemNames[r] = device->getDisplayName();
			if (d == soundEditor.currentValue) selectedRow = r;
			r++;
		}
		d++;
	}

	while (r < OLED_MENU_NUM_OPTIONS_VISIBLE) {
		itemNames[r] = NULL;
		r++;
	}

	drawItemsForOled(itemNames, selectedRow);
}

#endif
