/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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
#include "menu_item.h"
#include "range.h"

namespace deluge::gui::menu_item {

class MultiRange final : public Range {
public:
	using Range::Range;

	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void selectEncoderAction(int32_t offset) override;
	MenuItem* selectButtonPress() override;
	void noteOnToChangeRange(int32_t noteCode);
	bool isRangeDependent() override { return true; }
	void deletePress();
	MenuItem* menuItemHeadingTo;

protected:
	void getText(char* buffer, int32_t* getLeftLength = nullptr, int32_t* getRightLength = nullptr,

	             bool mayShowJustOne = true) override;
	bool mayEditRangeEdge(RangeEdit whichEdge) override;

	[[nodiscard]] std::string_view getTitle() const override {
		return l10n::getView(l10n::String::STRING_FOR_NOTE_RANGE);
	};
	void drawPixelsForOled() override;
};

extern MultiRange multiRangeMenu;
} // namespace deluge::gui::menu_item
