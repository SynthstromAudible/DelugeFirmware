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

#include "gui/menu_item/selection/selection.h"
#include "zone_selector.h"

namespace deluge::gui::menu_item::mpe {

class DirectionSelector final : public Selection<2> {
public:
	using Selection::Selection;
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	static_vector<string, capacity()> getOptions() override { return {"In", "Out"}; }
	void readCurrentValue() override { this->value_ = whichDirection; }
	void writeCurrentValue() override { whichDirection = this->value_; }
	MenuItem* selectButtonPress() override;
	uint8_t whichDirection;
#if HAVE_OLED
	[[nodiscard]] const string& getTitle() const override {
		return whichDirection ? "MPE output" : "MPE input";
	}
#endif
};

extern DirectionSelector directionSelectorMenu;

} // namespace deluge::gui::menu_item::mpe
