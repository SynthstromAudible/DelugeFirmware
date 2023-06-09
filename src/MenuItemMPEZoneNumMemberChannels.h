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

#ifndef MENUITEMMPEZONENUMMEMBERCHANNELS_H_
#define MENUITEMMPEZONENUMMEMBERCHANNELS_H_

#include "MenuItemInteger.h"
#include "MIDIDeviceManager.h"

class MIDIPort;

class MenuItemMPEZoneNumMemberChannels final : public MenuItemIntegerWithOff {
public:
	MenuItemMPEZoneNumMemberChannels();
	/*
#if HAVE_OLED
	void beginSession(MenuItem* navigatedBackwardFrom);
#endif
*/
	int getMaxValue();
	void readCurrentValue();
	void writeCurrentValue();
	//char nameChars[16];

private:
	MIDIPort* getPort();
};

extern MenuItemMPEZoneNumMemberChannels mpeZoneNumMemberChannelsMenu;

#endif /* MENUITEMMPEZONENUMMEMBERCHANNELS_H_ */
