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
	syncValueToString(this->getValue(), buffer, currentSong->getInputTickMagnitude());
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

void SyncLevel::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	// Render item name

	std::string_view name = getName();
	size_t nameLen = std::min((size_t)(width / kTextSpacingX), name.size());
	// If we can fit the whole name, we do, if we can't we chop one letter off. It just looks and
	// feels better, at least with the names we have now.
	if (name.size() > nameLen) {
		nameLen -= 1;
	}
	DEF_STACK_STRING_BUF(shortName, 10);
	for (uint8_t p = 0; p < nameLen; p++) {
		shortName.append(name[p]);
	}
	image.drawString(shortName.c_str(), startX, startY, kTextSpacingX, kTextSpacingY, 0, startX + width);

	// Render current value

	char const* text = l10n::get(l10n::String::STRING_FOR_OFF);
	DEF_STACK_STRING_BUF(shortOpt, 6);
	int32_t pxLen;
	if (this->getValue() != 0) {
		DEF_STACK_STRING_BUF(opt, 30);
		getNoteLengthName(opt);
		// Grab 6-char prefix with spaces removed.
		for (uint8_t p = 0; p < opt.size() && shortOpt.size() < shortOpt.capacity(); p++) {
			if (opt.data()[p] != ' ' && opt.data()[p] != '-') {
				shortOpt.append(opt.data()[p]);
			}
		}
		// Trim characters from the end until it fits.
		while ((pxLen = image.getStringWidthInPixels(shortOpt.c_str(), kTextTitleSizeY)) >= width) {
			shortOpt.data()[shortOpt.size() - 1] = 0;
		}
		text = shortOpt.data();
	}
	else {
		pxLen = image.getStringWidthInPixels(text, kTextTitleSizeY);
	}
	// Padding to center the string. If we can't center exactly, 1px right is better than 1px left.
	int32_t pad = (width + 1 - pxLen) / 2;
	image.drawString(text, startX + pad, startY + kTextSpacingY + 2, kTextTitleSpacingX, kTextTitleSizeY, 0,
	                 startX - pad + width);
}

} // namespace deluge::gui::menu_item
