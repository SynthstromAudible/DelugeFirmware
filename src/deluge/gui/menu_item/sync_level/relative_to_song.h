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
#include "util/functions.h"

namespace deluge::gui::menu_item::sync_level {

// This one is "relative to the song". In that it'll show its text value to the user, e.g. "16ths", regardless of any
// song variables, and then when its value gets used for anything, it'll be transposed into the song's magnitude by
// adding / subtracting the song's insideWorldTickMagnitude
class RelativeToSong : public SyncLevel {
public:
	using SyncLevel::SyncLevel;

protected:
	void getNoteLengthName(StringBuf& buffer) final {
		getNoteLengthNameFromMagnitude(buffer, -6 + 9 - this->getValue());
	}
};
} // namespace deluge::gui::menu_item::sync_level
