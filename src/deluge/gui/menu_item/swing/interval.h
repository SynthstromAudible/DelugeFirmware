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
#include "gui/ui/sound_editor.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::swing {

class Interval final : public SyncLevel {
public:
	using SyncLevel::SyncLevel;

	void readCurrentValue() override { this->set_value(currentSong->swingInterval); }
	void writeCurrentValue() override { currentSong->changeSwingInterval(this->get_value()); }

	void selectEncoderAction(int32_t offset) override { // So that there's no "off" option
		this->set_value(this->get_value() + offset);
		int32_t numOptions = this->size();

		// Wrap value
		if (this->get_value() >= numOptions) {
			this->set_value(this->get_value() - (numOptions - 1));
		}
		else if (this->get_value() < 1) {
			this->set_value(this->get_value() + (numOptions - 1));
		}

		Value::selectEncoderAction(offset);
	}
};

} // namespace deluge::gui::menu_item::swing
