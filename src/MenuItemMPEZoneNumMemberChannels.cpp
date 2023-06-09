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

#include <MenuItemMPEZoneNumMemberChannels.h>
#include "soundeditor.h"
#include "MIDIDevice.h"
#include "MenuItemMPEZoneSelector.h"
#include "MenuItemMPEDirectionSelector.h"
#include "midiengine.h"
#include "string.h"

MenuItemMPEZoneNumMemberChannels mpeZoneNumMemberChannelsMenu;

MenuItemMPEZoneNumMemberChannels::MenuItemMPEZoneNumMemberChannels() {
#if HAVE_OLED
	basicTitle = "Num member ch.";
#endif
}

MIDIPort* MenuItemMPEZoneNumMemberChannels::getPort() {
	return &soundEditor.currentMIDIDevice->ports[mpeDirectionSelectorMenu.whichDirection];
}

int MenuItemMPEZoneNumMemberChannels::getMaxValue() {
	MIDIPort* port = getPort();

	// This limits it so we don't eat into the other zone
	int numChannelsAvailable;
	if (mpeZoneSelectorMenu.whichZone == MPE_ZONE_LOWER_NUMBERED_FROM_0) {
		numChannelsAvailable = port->mpeUpperZoneLastMemberChannel;
	}
	else {
		numChannelsAvailable = 15 - port->mpeLowerZoneLastMemberChannel;
	}

	int numMemberChannelsAvailable = numChannelsAvailable - 1;
	if (numMemberChannelsAvailable < 0) numMemberChannelsAvailable = 0;
	if (numMemberChannelsAvailable == 14) numMemberChannelsAvailable = 15;

	return numMemberChannelsAvailable;
}

void MenuItemMPEZoneNumMemberChannels::readCurrentValue() {
	MIDIPort* port = getPort();
	if (mpeZoneSelectorMenu.whichZone == MPE_ZONE_LOWER_NUMBERED_FROM_0) {
		soundEditor.currentValue = port->mpeLowerZoneLastMemberChannel;
	}
	else {
		soundEditor.currentValue = 15 - port->mpeUpperZoneLastMemberChannel;
	}
}

void MenuItemMPEZoneNumMemberChannels::writeCurrentValue() {
	MIDIPort* port = getPort();
	if (mpeZoneSelectorMenu.whichZone == MPE_ZONE_LOWER_NUMBERED_FROM_0) {
		port->mpeLowerZoneLastMemberChannel = soundEditor.currentValue;
	}
	else {
		port->mpeUpperZoneLastMemberChannel = 15 - soundEditor.currentValue;
	}

	MIDIDeviceManager::recountSmallestMPEZones();
	MIDIDeviceManager::anyChangesToSave = true;

	// If this was for an output, we transmit an MCM message to tell the device about our MPE zone for the MIDI they'll be receiving from us.
	if (mpeDirectionSelectorMenu.whichDirection == MIDI_DIRECTION_OUTPUT_FROM_DELUGE) {
		int masterChannel = (mpeZoneSelectorMenu.whichZone == MPE_ZONE_LOWER_NUMBERED_FROM_0) ? 0 : 15;

		soundEditor.currentMIDIDevice->sendRPN(masterChannel, 0, 6, soundEditor.currentValue);
	}
}
