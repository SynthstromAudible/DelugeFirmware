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
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "hid/led/pad_leds.h"
#include "storage/flash_storage.h"

namespace deluge::gui::menu_item::flash {
class Status final : public Selection<3> {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->set_value(PadLEDs::flashCursor); }
	void writeCurrentValue() override {
		if (PadLEDs::flashCursor == FLASH_CURSOR_SLOW) {
			PadLEDs::clearTickSquares();
		}
		PadLEDs::flashCursor = this->get_value();
	}
	static_vector<string, capacity()> getOptions() override { return {"Fast", "Off", "Slow"}; }
};
} // namespace deluge::gui::menu_item::flash
