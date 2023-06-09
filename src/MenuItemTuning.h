/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
 * Copyright © 2023 Casey Tucker
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

#ifndef MENUITEMTUNING_H_
#define MENUITEMTUNING_H_

#include "MenuItemDecimal.h"
#include "MenuItemSelection.h"
#include "TuningSystem.h"

extern const char *tuningBankNames[NUM_TUNING_BANKS+2];

class MenuItemTuningNote : public MenuItemDecimal {
public:
	MenuItemTuningNote(char const* newName = NULL) : MenuItemDecimal(newName) {}
	int getMinValue() { return -5000; }
	int getMaxValue() { return 5000; }
	int getNumDecimalPlaces() { return 2; }
	void readCurrentValue() { tuningSystem.currentValue = (int32_t)tuningSystem.offsets[tuningSystem.currentNote]; }
	void writeCurrentValue(){ tuningSystem.setOffset(tuningSystem.currentNote, tuningSystem.currentValue); }
};

class MenuItemTuningBank : public MenuItemSelection {
public:
	MenuItemTuningBank(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { tuningSystem.currentValue = selectedTuningBank; }
	void writeCurrentValue() { selectedTuningBank = tuningSystem.currentValue; }
	int getNumOptions() { return NUM_TUNING_BANKS + 2; }
	char const** getOptions() { return tuningBankNames; }
};

#endif
