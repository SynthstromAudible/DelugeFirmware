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
#include "processing/engines/cv_engine.h"
#include "gui/menu_item/decimal.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::cv {
class Transpose final : public Decimal {
public:
	using Decimal::Decimal;
	int getMinValue() const { return -9600; }
	int getMaxValue() const { return 9600; }
	int getNumDecimalPlaces() const { return 2; }
	void readCurrentValue() {
		soundEditor.currentValue = (int32_t)cvEngine.cvChannels[soundEditor.currentSourceIndex].transpose * 100
		                           + cvEngine.cvChannels[soundEditor.currentSourceIndex].cents;
	}
	void writeCurrentValue() {
		int currentValue = soundEditor.currentValue + 25600;

		int semitones = (currentValue + 50) / 100;
		int cents = currentValue - semitones * 100;
		cvEngine.setCVTranspose(soundEditor.currentSourceIndex, semitones - 256, cents);
	}
};

} // namespace menu_item::cv
