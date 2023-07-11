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
#include "gui/menu_item/decimal.h"
#include "gui/ui/sound_editor.h"
#include "processing/engines/cv_engine.h"

namespace deluge::gui::menu_item::gate {
class OffTime final : public Decimal {
public:
	using Decimal::Decimal;
	int getMinValue() const { return 1; }
	int getMaxValue() const { return 100; }
	int getNumDecimalPlaces() const { return 1; }
	int getDefaultEditPos() { return 1; }
	void readCurrentValue() { soundEditor.currentValue = cvEngine.minGateOffTime; }
	void writeCurrentValue() { cvEngine.minGateOffTime = soundEditor.currentValue; }
};
} // namespace deluge::gui::menu_item::gate
