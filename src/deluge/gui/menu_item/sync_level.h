/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "gui/menu_item/enumeration.h"
#include "gui/menu_item/selection.h"

namespace deluge::gui::menu_item {

// This one is "absolute" - if song's insideWorldTickMagnitude changes, such a param's text value will display as a different one, but the music will sound the same
class SyncLevel : public Enumeration<28> {
public:
	using Enumeration::Enumeration;
	SyncType menuOptionToSyncType(int32_t option);
	::SyncLevel menuOptionToSyncLevel(int32_t option);
	int32_t syncTypeAndLevelToMenuOption(SyncType type, ::SyncLevel level);

protected:
	void drawValue() final;
	virtual void getNoteLengthName(char* buffer);

	void drawPixelsForOled() override;
};

} // namespace deluge::gui::menu_item
