/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#pragma once
#include "gui/menu_item/sync_level.h"

namespace deluge::gui::menu_item::arpeggiator {
class SequenceLength final : public ArpUnpatchedParam {
public:
	using ArpUnpatchedParam::ArpUnpatchedParam;

	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		if (getValue() == 0) {
			const auto off_string = l10n::get(l10n::String::STRING_FOR_OFF);
			return OLED::main.drawStringCentered(off_string, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
			                                     kTextSpacingX, kTextSpacingY, slot.width);
		}
		ArpUnpatchedParam::renderInHorizontalMenu(slot);
	}

	void getNotificationValue(StringBuf& valueBuf) override {
		if (const auto value = getValue(); value == 0) {
			valueBuf.append(l10n::get(l10n::String::STRING_FOR_OFF));
		}
		else {
			valueBuf.appendInt(value);
		}
	}
};

} // namespace deluge::gui::menu_item::arpeggiator
