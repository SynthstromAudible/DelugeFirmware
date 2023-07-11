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

#pragma once

#include "gui/menu_item/decimal.h"
#include "gui/menu_item/selection.h"
#include "model/tuning.h"
#include "gui/ui/sound_editor.h"
#include "util/functions.h"

namespace menu_item {

extern const char* tuningBankNames[NUM_TUNING_BANKS + 2];

class TuningReference : public Decimal {
public:
	TuningReference(char const* newName = NULL) : Decimal(newName) {}
	int getMinValue() const final { return 4000; }
	int getMaxValue() const final { return 4598; } // wants to go higher, but currently exceeds signed 32-bit integer range
	int getNumDecimalPlaces() const final { return 1; }
	void readCurrentValue() { soundEditor.currentValue = tuningSystem.getReference(); }
	void writeCurrentValue() { tuningSystem.setReference(soundEditor.currentValue); }
};

class TuningNote : public Decimal {
public:
	TuningNote(char const* newName = NULL) : Decimal(newName) {}
	int getMinValue() const final { return -5000; }
	int getMaxValue() const final { return 5000; }
	int getNumDecimalPlaces() const final { return 2; }
	void readCurrentValue() { soundEditor.currentValue = (int32_t)tuningSystem.offsets[tuningSystem.currentNote]; }
	void writeCurrentValue() { tuningSystem.setOffset(tuningSystem.currentNote, soundEditor.currentValue); }
};

class TuningBank : public Selection {
public:
	TuningBank(char const* newName = NULL) : Selection(newName) {}
	void beginSession(MenuItem* navigatedBackwardFrom);
	void readCurrentValue() { soundEditor.currentValue = selectedTuningBank; }
	void writeCurrentValue() { selectedTuningBank = soundEditor.currentValue; }
	void loadTuningsFromCard();
	int getNumOptions() { return NUM_TUNING_BANKS + 2; }
	char const** getOptions() { return tuningBankNames; }
};

#if HAVE_OLED
static char const* octaveNotes[] = {"E", "F", "F#", "G", "G#", "A", "A#", "B", "C", "C#", "D", "D#", NULL};
#else
static char const* octaveNotes[] = {"E", "F", "F.", "G", "G.", "A", "A.", "B", "C", "C.", "D", "D.", NULL};
#endif

extern TuningNote tuningNoteMenu;

class TuningNotes final : public Selection {
public:
	TuningNotes(char const* newName = NULL) : Selection(newName) {
#if HAVE_OLED
		basicTitle = "NOTES";
#endif
		basicOptions = octaveNotes;
	}
	void beginSession(MenuItem* navigatedBackwardFrom) {
		if (!navigatedBackwardFrom) soundEditor.currentValue = 0;
		else soundEditor.currentValue = tuningSystem.currentNote;
		Selection::beginSession(navigatedBackwardFrom);
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

} // namespace menu_item
