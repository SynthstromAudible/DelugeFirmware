/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "gui/menu_item/menu_item.h"

namespace deluge::gui::menu_item::midi {

// A Load or Save action for one fan-out group's templates. Selecting it records which group the
// browser should act on (MIDIFanOut::templateGroupIndex) and opens the load/save browser UI.
class FanOutTemplate final : public MenuItem {
public:
	FanOutTemplate(l10n::String newName, int32_t newGroup, bool newIsSave)
	    : MenuItem(newName), group(newGroup), isSave(newIsSave) {}
	// Open the browser as soon as the item is entered (beginSession), and arrange to pop back up to
	// the parent group menu when the browser closes - so there's no empty intermediate page.
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	MenuItem* selectButtonPress() override;

private:
	void openBrowser();
	int32_t group;
	bool isSave;
};
} // namespace deluge::gui::menu_item::midi
