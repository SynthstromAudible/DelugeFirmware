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

#include "processing/engines/audio_engine.h"
#include "model/clip/instrument_clip.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "submenu.h"
#include "hid/display/numeric_driver.h"
#include "gui/ui/sound_editor.h"
#include "model/song/song.h"
#include "model/instrument/instrument.h"
#include <array>

extern "C" {
#if HAVE_OLED
#include "RZA1/oled/oled_low_level.h"
#endif
}

namespace deluge::gui::menu_item {

void Submenu::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.currentSubmenuItem = items;
	soundEditor.menuCurrentScroll = 0;
	soundEditor.currentMultiRange = nullptr;
	if (navigatedBackwardFrom != nullptr) {
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
		if (*soundEditor.currentSubmenuItem == nullptr) { // Not sure we need this since we don't wrap submenu items?
			soundEditor.currentSubmenuItem = items;
		}
	}
#if !HAVE_OLED
	updateDisplay();
#endif
}

void Submenu::updateDisplay() {
#if HAVE_OLED
	renderUIsForOled();
#else
	(*soundEditor.currentSubmenuItem)->drawName();
#endif
}

#if HAVE_OLED
void Submenu::drawPixelsForOled() {
	std::array<char const*, OLED_MENU_NUM_OPTIONS_VISIBLE> itemNames = {};

	int selectedRow = soundEditor.menuCurrentScroll;
	itemNames[selectedRow] = (*soundEditor.currentSubmenuItem)->getName();

	MenuItem** thisSubmenuItem = soundEditor.currentSubmenuItem;
	size_t idx;
	for (idx = selectedRow + 1; idx < OLED_MENU_NUM_OPTIONS_VISIBLE; idx++) {

		// This finds the next relevant submenu item
		// TODO: this should be a forward_iterator
		while (thisSubmenuItem != nullptr) {
			thisSubmenuItem++;
			if (*thisSubmenuItem == nullptr) {
				goto searchBack;
			}

			if ((*thisSubmenuItem)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex)) {
				break;
			}
		}

		itemNames[idx] = (*thisSubmenuItem)->getName();
	}

searchBack:
	thisSubmenuItem = soundEditor.currentSubmenuItem;
	for (int i = selectedRow - 1; i >= 0; i--) {
		// This finds the prior relevant submenu item
		// TODO: this should be a reverse_iterator
		while (thisSubmenuItem != nullptr) {
			if (thisSubmenuItem == this->items) { // Back at start, so render and return.
				drawItemsForOled({itemNames.data(), idx}, selectedRow);
				return;
			}
			thisSubmenuItem--;

			if ((*thisSubmenuItem)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex)) {
				break;
			}
		}

		itemNames[i] = (*thisSubmenuItem)->getName();
	}

	drawItemsForOled({itemNames.data(), idx}, selectedRow);
}
#endif

void Submenu::selectEncoderAction(int offset) {

	MenuItem** thisSubmenuItem = soundEditor.currentSubmenuItem;

	do {
		if (offset >= 0) {
			thisSubmenuItem++;
			if (*thisSubmenuItem == nullptr) {
#if HAVE_OLED
				return;
#else
				thisSubmenuItem = items;
#endif
			}
		}
		else {
			if (thisSubmenuItem == items) {
#if HAVE_OLED
				return;
#else
				while (*(thisSubmenuItem + 1)) {
					thisSubmenuItem++;
				}
#endif
			}
			else {
				thisSubmenuItem--;
			}
		}
	} while (!(*thisSubmenuItem)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex));

	soundEditor.currentSubmenuItem = thisSubmenuItem;

#if HAVE_OLED
	soundEditor.menuCurrentScroll += offset;
	if (soundEditor.menuCurrentScroll < 0) {
		soundEditor.menuCurrentScroll = 0;
	}
	else if (soundEditor.menuCurrentScroll > OLED_MENU_NUM_OPTIONS_VISIBLE - 1) {
		soundEditor.menuCurrentScroll = OLED_MENU_NUM_OPTIONS_VISIBLE - 1;
	}
#endif

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

void Submenu::learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*soundEditor.currentSubmenuItem)->learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	}
}

bool Submenu::learnNoteOn(MIDIDevice* fromDevice, int channel, int noteCode) {
	if (soundEditor.getCurrentMenuItem() == this) {
		return (*soundEditor.currentSubmenuItem)->learnNoteOn(fromDevice, channel, noteCode);
	}
	return false;
}
} // namespace deluge::gui::menu_item
