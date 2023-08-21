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

#include "gui/l10n/l10n.h"
#include "gui/menu_item/selection.h"

namespace deluge::gui::menu_item::mpe {

class ZoneSelector final : public Selection<2> {
public:
	using Selection::Selection;
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void readCurrentValue() override { this->setValue(whichZone); }
	void writeCurrentValue() override { whichZone = this->getValue(); }

	static_vector<std::string_view, capacity()> getOptions() override {
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_LOWER_ZONE),
		    l10n::getView(STRING_FOR_UPPER_ZONE),
		};
	}

	MenuItem* selectButtonPress() override;
	uint8_t whichZone;
};

extern ZoneSelector zoneSelectorMenu;
} // namespace deluge::gui::menu_item::mpe
