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

#include "MenuItemSyncLevel.h"
#include "soundeditor.h"
#include "numericdriver.h"
#include "song.h"
#include "oled.h"

void MenuItemSyncLevel::drawValue() {
	if (soundEditor.currentValue == 0) {
		numericDriver.setText("OFF");
	}
	else {
		char* buffer = shortStringBuffer;
		getNoteLengthName(buffer);

#if HAVE_OLED
		numericDriver.setText(buffer);
#else
		if (strlen(buffer) <= NUMERIC_DISPLAY_LENGTH) {
			numericDriver.setText(buffer, true);
		}
		else {
			numericDriver.setScrollingText(buffer, 0);
		}
#endif
	}
}

void MenuItemSyncLevel::getNoteLengthName(char* buffer) {
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
		if (strcmp(&buffer[2], "bar") == 0) {
			strcpy(buffer, "bar");
			strcat(buffer, type);
		}
		else {
			strcat(buffer, type);
		}
	}
}

void MenuItemSyncLevelRelativeToSong::getNoteLengthName(char* buffer) {
	getNoteLengthNameFromMagnitude(buffer, -6 + 9 - soundEditor.currentValue);
}

#if HAVE_OLED
void MenuItemSyncLevel::drawPixelsForOled() {
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

SyncType MenuItemSyncLevel::menuOptionToSyncType(int option) {
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

SyncLevel MenuItemSyncLevel::menuOptionToSyncLevel(int option) {
	SyncLevel level;
	if (option < SYNC_TYPE_TRIPLET) {
		level = (SyncLevel)option;
	}
	else if (option < SYNC_TYPE_DOTTED) {
		level = (SyncLevel)(option - SYNC_TYPE_TRIPLET + 1);
	}
	else {
		level = (SyncLevel)(option - SYNC_TYPE_DOTTED + 1);
	}
	return level;
}

int MenuItemSyncLevel::syncTypeAndLevelToMenuOption(SyncType type, SyncLevel level) {
	return (int)(type + level - (type != SYNC_TYPE_EVEN ? 1 : 0));
}
