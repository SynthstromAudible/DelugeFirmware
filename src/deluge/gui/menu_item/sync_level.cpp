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
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item {

void SyncLevel::drawValue() {
	if (this->getValue() == 0) {
		display->setText(l10n::get(l10n::String::STRING_FOR_DISABLED));
	}
	else {
		char* buffer = shortStringBuffer;
		getNoteLengthName(buffer);
		display->setScrollingText(buffer, 0);
	}
}

void SyncLevel::getNoteLengthName(char* buffer) {
	char type[7] = "";
	if (this->getValue() < SYNC_TYPE_TRIPLET) {
		currentSong->getNoteLengthName(buffer, (uint32_t)3 << (SYNC_LEVEL_256TH - this->getValue()));
	}
	else if (this->getValue() < SYNC_TYPE_DOTTED) {
		currentSong->getNoteLengthName(buffer,
		                               (uint32_t)3 << ((SYNC_TYPE_TRIPLET - 1) + SYNC_LEVEL_256TH - this->getValue()));
		strcpy(type, "-tplts");
	}
	else {
		currentSong->getNoteLengthName(buffer,
		                               (uint32_t)3 << ((SYNC_TYPE_DOTTED - 1) + SYNC_LEVEL_256TH - this->getValue()));
		strcpy(type, "-dtted");
	}
	if (strlen(type) > 0) {
		if (strcmp(&buffer[2], "bar") == 0) { // OLED `1-bar` -> '1-bar-trplts`
			strcat(buffer, type);
		}
		else {
			if (display->haveOLED()) {
				char* suffix = strstr(buffer, "-notes"); // OLED replace `-notes` with type,
				strcpy(suffix, type);                    //      eg. `2nd-notes` -> `2nd-trplts`
			}
			else {
				strcat(buffer, type); // 7SEG just append the type
			}
		}
	}
}

void SyncLevel::drawPixelsForOled() {
	char const* text = l10n::get(l10n::String::STRING_FOR_OFF);
	char buffer[30];
	if (this->getValue()) {
		text = buffer;
		getNoteLengthName(buffer);
	}
	deluge::hid::display::OLED::drawStringCentred(text, 20 + OLED_MAIN_TOPMOST_PIXEL,
	                                              deluge::hid::display::OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
	                                              kTextBigSpacingX, kTextBigSizeY);
}

SyncType SyncLevel::menuOptionToSyncType(int32_t option) {
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

::SyncLevel SyncLevel::menuOptionToSyncLevel(int32_t option) {
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

int32_t SyncLevel::syncTypeAndLevelToMenuOption(::SyncType type, ::SyncLevel level) {
	return static_cast<int32_t>(type) + (static_cast<int32_t>(level) - (type != SYNC_TYPE_EVEN ? 1 : 0));
}
} // namespace deluge::gui::menu_item
