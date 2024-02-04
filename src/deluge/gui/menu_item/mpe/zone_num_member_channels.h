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

#pragma once

#include "gui/menu_item/integer.h"

class MIDIPort;

namespace deluge::gui::menu_item::mpe {
class ZoneNumMemberChannels final : public IntegerWithOff {
public:
	using IntegerWithOff::IntegerWithOff;
	// void beginSession(MenuItem* navigatedBackwardFrom);
	[[nodiscard]] int32_t getMaxValue() const override;
	void readCurrentValue() override;
	void writeCurrentValue() override;
	// char nameChars[16];

	[[nodiscard]] std::string_view getTitle() const override {
		return l10n::getView(l10n::String::STRING_FOR_NUM_MEMBER_CH_MENU_TITLE);
	}

private:
	[[nodiscard]] MIDIPort* getPort() const;
};

extern ZoneNumMemberChannels zoneNumMemberChannelsMenu;
} // namespace deluge::gui::menu_item::mpe
