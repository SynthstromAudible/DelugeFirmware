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
#include "gui/menu_item/submenu.h"
#include "model/clip/clip.h"
#include "model/song/song.h"
#include "model/output.h"

namespace menu_item::submenu {
class Bend final : public Submenu {
public:
	Bend(char const* newName = nullptr, MenuItem** newItems = nullptr) : Submenu(newName, newItems) {}
	bool isRelevant(Sound* sound, int whichThing) override {
		// Drums within a Kit don't need the two-item submenu - they have their own single item.
		const auto type = currentSong->currentClip->output->type;
		return (type == INSTRUMENT_TYPE_SYNTH || type == INSTRUMENT_TYPE_CV);
	}
};
} // namespace menu_item::submenu
