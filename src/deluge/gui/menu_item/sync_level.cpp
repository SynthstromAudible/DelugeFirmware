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
		display->setText(l10n::get(l10n::String::STRING_FOR_OFF));
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
	hid::display::OLED::main.drawStringCentred(text, 20 + OLED_MAIN_TOPMOST_PIXEL, kTextBigSpacingX, kTextBigSizeY);
}

void SyncLevel::configureRenderingOptions(const HorizontalMenuRenderingOptions& options) {
	Enumeration::configureRenderingOptions(options);

	const int32_t value = getValue();
	const ::SyncLevel level = syncValueToSyncLevel(value);

	if (level != SYNC_LEVEL_NONE) {
		// Draw the sync level as a label
		DEF_STACK_STRING_BUF(label_buf, 4);
		syncValueToStringForHorzMenuLabel(syncValueToSyncType(value), level, label_buf,
		                                  currentSong->getInputTickMagnitude());
		options.label = label_buf.data();
	}
}

void SyncLevel::renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) {
	using namespace deluge::hid::display;
	oled_canvas::Canvas& image = OLED::main;

	const int32_t value = getValue();

	if (const ::SyncLevel level = syncValueToSyncLevel(value); level == SYNC_LEVEL_NONE) {
		const auto off_string = l10n::get(l10n::String::STRING_FOR_OFF);
		return image.drawStringCentered(off_string, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
		                                kTextSpacingX, kTextSpacingY, slot.width);
	}

	// Draw only the sync type icon, sync level already drawn as a label
	const Icon& type_icon = [&] {
		switch (syncValueToSyncType(getValue())) {
		case SYNC_TYPE_EVEN:
			return OLED::syncTypeEvenIcon;
		case SYNC_TYPE_DOTTED:
			return OLED::syncTypeDottedIcon;
		case SYNC_TYPE_TRIPLET:
			return OLED::syncTypeTripletsIcon;
		}
		return OLED::syncTypeEvenIcon;
	}();
	image.drawIconCentered(type_icon, slot.start_x, slot.width, slot.start_y + kHorizontalMenuSlotYOffset - 3);
}

int32_t SyncLevel::syncTypeAndLevelToMenuOption(::SyncType type, ::SyncLevel level) {
	return static_cast<int32_t>(type) + (static_cast<int32_t>(level) - (type != SYNC_TYPE_EVEN ? 1 : 0));
}

void SyncLevel::getShortOption(StringBuf& opt) {
	// Note length name trimmed to fit, or OFF
	if (this->getValue() != 0) {
		getNoteLengthName(opt);
	}
	else {
		opt.append(l10n::get(l10n::String::STRING_FOR_OFF));
	}
}

} // namespace deluge::gui::menu_item
