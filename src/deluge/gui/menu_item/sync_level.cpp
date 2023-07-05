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

#include "sync_level.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include "model/song/song.h"
#include "hid/display/oled.h"

namespace menu_item {

void SyncLevel::drawValue() {
	if (soundEditor.currentValue == 0) {
		numericDriver.setText("OFF");
	}
	else {
		char* buffer = shortStringBuffer;
		getNoteLengthName(buffer);

#if HAVE_OLED
		numericDriver.setText(buffer);
#else
		numericDriver.setScrollingText(buffer, 0);
#endif
	}
}

void SyncLevel::getNoteLengthName(char* buffer) {
	char type[7] = "";
	if (soundEditor.currentValue < SYNC_TYPE_TRIPLET) {
		currentSong->getNoteLengthName(buffer, (uint32_t)3 << (SYNC_LEVEL_256TH - soundEditor.currentValue));
	}
	else if (soundEditor.currentValue < SYNC_TYPE_DOTTED) {
		currentSong->getNoteLengthName(
		    buffer, (uint32_t)3 << ((SYNC_TYPE_TRIPLET - 1) + SYNC_LEVEL_256TH - soundEditor.currentValue));
		strcpy(type, "-tplts");
	}
	else {
		currentSong->getNoteLengthName(
		    buffer, (uint32_t)3 << ((SYNC_TYPE_DOTTED - 1) + SYNC_LEVEL_256TH - soundEditor.currentValue));
		strcpy(type, "-dtted");
	}
	if (strlen(type) > 0) {
		if (strcmp(&buffer[2], "bar") == 0) { // OLED `1-bar` -> '1-bar-trplts`
			strcat(buffer, type);
		}
		else {
#if HAVE_OLED
			char* suffix = strstr(buffer, "-notes"); // OLED replace `-notes` with type,
			strcpy(suffix, type);                    //      eg. `2nd-notes` -> `2nd-trplts`
#else
			strcat(buffer, type); // 7SEG just append the type
#endif
		}
	}
}

#if HAVE_OLED
void SyncLevel::drawPixelsForOled() {
	char const* text = "Off";
	char buffer[30];
	if (soundEditor.currentValue) {
		text = buffer;
		getNoteLengthName(buffer);
	}
	OLED::drawStringCentred(text, 20 + OLED_MAIN_TOPMOST_PIXEL, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
	                        TEXT_BIG_SPACING_X, TEXT_BIG_SIZE_Y);
}
#endif

SyncType SyncLevel::menuOptionToSyncType(int option) {
	if (option < SYNC_TYPE_TRIPLET) {
		return SYNC_TYPE_EVEN;
	}
	else if (option < SYNC_TYPE_DOTTED) {
		return SYNC_TYPE_TRIPLET;
	}
	else {
		return SYNC_TYPE_DOTTED;
	}
}

::SyncLevel SyncLevel::menuOptionToSyncLevel(int option) {
	::SyncLevel level;
	if (option < SYNC_TYPE_TRIPLET) {
		level = static_cast<::SyncLevel>(option);
	}
	else if (option < SYNC_TYPE_DOTTED) {
		level = static_cast<::SyncLevel>(option - SYNC_TYPE_TRIPLET + 1);
	}
	else {
		level = static_cast<::SyncLevel>(option - SYNC_TYPE_DOTTED + 1);
	}
	return level;
}

int SyncLevel::syncTypeAndLevelToMenuOption(::SyncType type, ::SyncLevel level) {
	return static_cast<int>(type) + (static_cast<int>(level) - (type != SYNC_TYPE_EVEN ? 1 : 0));
}
} // namespace menu_item
