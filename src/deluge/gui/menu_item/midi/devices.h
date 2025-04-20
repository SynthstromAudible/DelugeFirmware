/*
 * Copyright © 2021-2025 Synthstrom Audible Limited
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
#include <cstddef>

namespace deluge::gui::menu_item::midi {

/// Menu item for the MIDI device selection menu.
class Devices final : public MenuItem {
public:
	using MenuItem::MenuItem;
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void selectEncoderAction(int32_t offset) override;
	MIDICable* getCable(int32_t deviceIndex);
	MenuItem* selectButtonPress() override;

	void drawValue();
	void drawPixelsForOled() override;

private:
	/// The currently selected cable. This is either 0 (for the DIN ports) or 1 + the index into the root USB cables.
	int32_t current_cable_;
	/// Scroll position within the displayed items for the OLED
	int32_t scroll_pos_;
};

extern Devices devicesMenu;
} // namespace deluge::gui::menu_item::midi
