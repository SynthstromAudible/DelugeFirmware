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
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item {

void SyncLevel::drawValue() {
	if (this->getValue() == 0) {
		display->setText(l10n::get(l10n::String::STRING_FOR_DISABLED));
	}
	else {
		StringBuf buffer{shortStringBuffer, kShortStringBufferSize};
		getNoteLengthName(buffer);
		display->setScrollingText(buffer.data(), 0);
	}
}

void SyncLevel::getNoteLengthName(StringBuf& buffer) {
	char const* typeStr = nullptr;
	uint32_t value = this->getValue();
	::SyncType type{menuOptionToSyncType(value)};
	::SyncLevel level{menuOptionToSyncLevel(value)};

	uint32_t shift = SYNC_LEVEL_256TH - level;
	uint32_t noteLength = uint32_t{3} << shift;

	switch (type) {
	case SYNC_TYPE_EVEN:
		if (value != 0) {
			typeStr = "-notes";
		}
		break;
	case SYNC_TYPE_TRIPLET:
		typeStr = "-tplts";
		break;
	case SYNC_TYPE_DOTTED:
		typeStr = "-dtted";
		break;
	}

	currentSong->getNoteLengthName(buffer, noteLength, typeStr);
	if (typeStr != nullptr) {
		int32_t magnitudeLevelBars = SYNC_LEVEL_8TH - currentSong->insideWorldTickMagnitude;
		if (((type == SYNC_TYPE_TRIPLET || type == SYNC_TYPE_DOTTED) && level <= magnitudeLevelBars)
		    || display->have7SEG()) {
			// On OLED, getNoteLengthName handles adding this for the non-bar levels. On 7seg, always append it
			buffer.append(typeStr);
		}
	}
}

void SyncLevel::drawPixelsForOled() {
	char const* text = l10n::get(l10n::String::STRING_FOR_OFF);
	DEF_STACK_STRING_BUF(buffer, 30);
	if (this->getValue() != 0) {
		text = buffer.data();
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
