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
#include "model/mod_controllable/mod_controllable_audio.h"
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::fx {

class Clipping final : public IntegerWithOff {
public:
	using IntegerWithOff::IntegerWithOff;

	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentModControllable->clippingAmount; }
	void writeCurrentValue() { soundEditor.currentModControllable->clippingAmount = soundEditor.currentValue; }
	int getMaxValue() const { return 15; }
};

} // namespace menu_item::fx
