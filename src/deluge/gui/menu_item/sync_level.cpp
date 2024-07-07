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
	syncValueToString(this->getValue(), buffer);
}

void SyncLevel::drawPixelsForOled() {
	char const* text = l10n::get(l10n::String::STRING_FOR_OFF);
	DEF_STACK_STRING_BUF(buffer, 30);
	if (this->getValue() != 0) {
		text = buffer.data();
		getNoteLengthName(buffer);
	}
	deluge::hid::display::OLED::main.drawStringCentred(text, 20 + OLED_MAIN_TOPMOST_PIXEL, kTextBigSpacingX,
	                                                   kTextBigSizeY);
}

int32_t SyncLevel::syncTypeAndLevelToMenuOption(::SyncType type, ::SyncLevel level) {
	return static_cast<int32_t>(type) + (static_cast<int32_t>(level) - (type != SYNC_TYPE_EVEN ? 1 : 0));
}
} // namespace deluge::gui::menu_item
