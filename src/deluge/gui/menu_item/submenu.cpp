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

#include "submenu.h"
#include "gui/ui/sound_editor.h"
#include "hid/display.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

extern "C" {
#include "RZA1/oled/oled_low_level.h"
}

namespace menu_item {

void Submenu::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.currentSubmenuItem = items;
	soundEditor.menuCurrentScroll = 0;
	soundEditor.currentMultiRange = NULL;
	if (navigatedBackwardFrom) {
		while (*soundEditor.currentSubmenuItem != navigatedBackwardFrom) {
			if (!*soundEditor.currentSubmenuItem) { // If desired item not found
				soundEditor.currentSubmenuItem = items;
				break;
			}
			soundEditor.currentSubmenuItem++;
		}
	}
	while (!(*soundEditor.currentSubmenuItem)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex)) {
		soundEditor.currentSubmenuItem++;
		if (!*soundEditor.currentSubmenuItem) { // Not sure we need this since we don't wrap submenu items?
			soundEditor.currentSubmenuItem = items;
		}
	}
	if (display.type != DisplayType::OLED) {
		updateDisplay();
	}
}

void Submenu::updateDisplay() {
	if (display.type == DisplayType::OLED) {
		renderUIsForOled();
	}
	else {
		(*soundEditor.currentSubmenuItem)->drawName();
	}
}

void Submenu::drawPixelsForOled() {
	char const* itemNames[kOLEDMenuNumOptionsVisible];
	for (int32_t i = 0; i < kOLEDMenuNumOptionsVisible; i++) {
		itemNames[i] = NULL;
	}

	int32_t selectedRow = soundEditor.menuCurrentScroll;
	itemNames[selectedRow] = (*soundEditor.currentSubmenuItem)->getName();

	MenuItem** thisSubmenuItem = soundEditor.currentSubmenuItem;
	for (int32_t i = selectedRow + 1; i < kOLEDMenuNumOptionsVisible; i++) {
		do {
			thisSubmenuItem++;
			if (!*thisSubmenuItem) {
				goto searchBack;
			}
		} while (!(*thisSubmenuItem)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex));

		itemNames[i] = (*thisSubmenuItem)->getName();
	}

searchBack:
	thisSubmenuItem = soundEditor.currentSubmenuItem;
	for (int32_t i = selectedRow - 1; i >= 0; i--) {
		do {
			if (thisSubmenuItem == items) {
				goto doneSearching;
			}
			thisSubmenuItem--;
		} while (!(*thisSubmenuItem)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex));

		itemNames[i] = (*thisSubmenuItem)->getName();
	}

doneSearching:
	drawItemsForOled(itemNames, selectedRow);
}

void Submenu::selectEncoderAction(int32_t offset) {

	MenuItem** thisSubmenuItem = soundEditor.currentSubmenuItem;

	do {
		if (offset >= 0) {
			thisSubmenuItem++;
			if (!*thisSubmenuItem) {
				if (display.type == DisplayType::OLED) {
					return;
				}
				else {
					thisSubmenuItem = items;
				}
			}
		}
		else {
			if (thisSubmenuItem == items) {
				if (display.type == DisplayType::OLED) {
					return;
				}
				while (*(thisSubmenuItem + 1)) {
					thisSubmenuItem++;
				}
			}
			else {
				thisSubmenuItem--;
			}
		}
	} while (!(*thisSubmenuItem)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex));

	soundEditor.currentSubmenuItem = thisSubmenuItem;

	if (display.type == DisplayType::OLED) {
		soundEditor.menuCurrentScroll += offset;
		if (soundEditor.menuCurrentScroll < 0) {
			soundEditor.menuCurrentScroll = 0;
		}
		else if (soundEditor.menuCurrentScroll > kOLEDMenuNumOptionsVisible - 1) {
			soundEditor.menuCurrentScroll = kOLEDMenuNumOptionsVisible - 1;
		}
	}

	updateDisplay();
}

MenuItem* Submenu::selectButtonPress() {
	return *soundEditor.currentSubmenuItem;
}

void Submenu::unlearnAction() {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*soundEditor.currentSubmenuItem)->unlearnAction();
	}
}

bool Submenu::allowsLearnMode() {
	if (soundEditor.getCurrentMenuItem() == this) {
		return (*soundEditor.currentSubmenuItem)->allowsLearnMode();
	}
	return false;
}

void Submenu::learnKnob(MIDIDevice* fromDevice, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*soundEditor.currentSubmenuItem)->learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	}
}

bool Submenu::learnNoteOn(MIDIDevice* fromDevice, int32_t channel, int32_t noteCode) {
	if (soundEditor.getCurrentMenuItem() == this) {
		return (*soundEditor.currentSubmenuItem)->learnNoteOn(fromDevice, channel, noteCode);
	}
	return false;
}
} // namespace menu_item
