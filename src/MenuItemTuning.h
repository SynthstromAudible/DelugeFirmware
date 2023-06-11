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
#include "soundeditor.h"
#include "functions.h"

extern const char *tuningBankNames[NUM_TUNING_BANKS+2];

class MenuItemTuningReference : public MenuItemDecimal {
public:
	MenuItemTuningReference(char const* newName = NULL) : MenuItemDecimal(newName) {}
	int getMinValue() { return 4000; }
	int getMaxValue() { return 5000; }
	int getNumDecimalPlaces() { return 1; }
	void readCurrentValue() {
		soundEditor.currentValue = tuningSystem.getReference();
	}
	void writeCurrentValue(){
		tuningSystem.setReference(soundEditor.currentValue);
	}
};

class MenuItemTuningNote : public MenuItemDecimal {
public:
	MenuItemTuningNote(char const* newName = NULL) : MenuItemDecimal(newName) {}
	int getMinValue() { return -5000; }
	int getMaxValue() { return 5000; }
	int getNumDecimalPlaces() { return 2; }
	void readCurrentValue() {
		soundEditor.currentValue = (int32_t)tuningSystem.offsets[tuningSystem.currentNote];
	}
	void writeCurrentValue(){
		tuningSystem.setOffset(tuningSystem.currentNote, soundEditor.currentValue);
	}
};

class MenuItemTuningBank : public MenuItemSelection {
public:
	MenuItemTuningBank(char const* newName = NULL) : MenuItemSelection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = selectedTuningBank; }
	void writeCurrentValue() { selectedTuningBank = soundEditor.currentValue; }
	int getNumOptions() { return NUM_TUNING_BANKS + 2; }
	char const** getOptions() { return tuningBankNames; }
};

#if HAVE_OLED
static char const* octaveNotes[] = {
	"E",
	"F",
	"F#",
	"G",
	"G#",
	"A",
	"A#",
	"B",
	"C",
	"C#",
	"D",
	"D#",
	NULL
};
#else
static char const* octaveNotes[] = {
	"E",
	"F",
	"F.",
	"G",
	"G.",
	"A",
	"A.",
	"B",
	"C",
	"C.",
	"D",
	"D.",
	NULL
};
#endif

extern MenuItemTuningNote tuningNoteMenu;

class MenuItemTuningNotes final : public MenuItemSelection {
public:
	MenuItemTuningNotes(char const* newName = NULL) : MenuItemSelection(newName) {
#if HAVE_OLED
		basicTitle = "NOTES";
#endif
		basicOptions = octaveNotes;
	}
	void beginSession(MenuItem* navigatedBackwardFrom) {
		if (!navigatedBackwardFrom) soundEditor.currentValue = 0;
		else soundEditor.currentValue = tuningSystem.currentNote;
		MenuItemSelection::beginSession(navigatedBackwardFrom);
	}

	MenuItem* selectButtonPress() {
		tuningSystem.currentNote = soundEditor.currentValue;
		tuningSystem.currentValue = tuningSystem.offsets[tuningSystem.currentNote];
#if HAVE_OLED
		tuningNoteMenu.basicTitle = octaveNotes[soundEditor.currentValue];
		// noteTitle = octaveNotes[tuningSystem.currentNote];
#endif
		return &tuningNoteMenu;
	}
};

#endif
