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
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::arpeggiator {
class Octaves final : public Integer {
public:
	Octaves(char const* newName = NULL) : Integer(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentArpSettings->numOctaves; }
	void writeCurrentValue() { soundEditor.currentArpSettings->numOctaves = soundEditor.currentValue; }
	int getMinValue() const { return 1; }
	int getMaxValue() const { return 8; }
};
} // namespace menu_item::arpeggiator
