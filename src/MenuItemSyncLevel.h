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

#ifndef MENUITEMSYNCLEVEL_H_
#define MENUITEMSYNCLEVEL_H_

#include "MenuItemSelection.h"

// This one is "absolute" - if song's insideWorldTickMagnitude changes, such a param's text value will display as a different one, but the music will sound the same
class MenuItemSyncLevel : public MenuItemSelection {
public:
	MenuItemSyncLevel(char const* newName = NULL) : MenuItemSelection(newName) {}
	SyncType menuOptionToSyncType(int option);
	SyncLevel menuOptionToSyncLevel(int option);
	int syncTypeAndLevelToMenuOption(SyncType type, SyncLevel level);

protected:
	int getNumOptions() { return 28; }
	void drawValue() final;
	virtual void getNoteLengthName(char* buffer);
#if HAVE_OLED
	void drawPixelsForOled();
#endif
};

// This one is "relative to the song". In that it'll show its text value to the user, e.g. "16ths", regardless of any song variables, and
// then when its value gets used for anything, it'll be transposed into the song's magnitude by adding / subtracting the song's insideWorldTickMagnitude
class MenuItemSyncLevelRelativeToSong : public MenuItemSyncLevel {
public:
	MenuItemSyncLevelRelativeToSong(char const* newName = NULL) : MenuItemSyncLevel(newName) {}

protected:
	void getNoteLengthName(char* buffer) final;
};

#endif /* MENUITEMSYNCLEVEL_H_ */
