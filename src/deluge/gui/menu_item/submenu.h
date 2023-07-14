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

#include "gui/menu_item/menu_item.h"
#include "menu_item.h"
#include "processing/engines/audio_engine.h"
#include "model/clip/instrument_clip.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "hid/display/numeric_driver.h"
#include "gui/ui/sound_editor.h"
#include "model/song/song.h"
#include "model/instrument/instrument.h"
#include "util/container/static_vector.hpp"
#include <array>
#include <initializer_list>

extern "C" {
#if HAVE_OLED
#include "RZA1/oled/oled_low_level.h"
#endif
}

namespace deluge::gui::menu_item {

template <size_t n>
class Submenu : public MenuItem {
public:
	Submenu(char const* newName, MenuItem* const (&newItems)[n])
	    : MenuItem(newName), items{to_static_vector(newItems)} {}
	Submenu(char const* newName, char const* title, MenuItem* const (&newItems)[n])
	    : MenuItem(newName, title), items{to_static_vector(newItems)} {}
	Submenu(char const* newName, char const* title, std::array<MenuItem*, n> newItems)
	    : MenuItem(newName, title), items{newItems.begin(), newItems.end()} {}

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void updateDisplay();
	void selectEncoderAction(int offset) final;
	MenuItem* selectButtonPress() final;
	void readValueAgain() final { updateDisplay(); }
	void unlearnAction() final;
	bool allowsLearnMode() final;
	void learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) final;
	bool learnNoteOn(MIDIDevice* fromDevice, int channel, int noteCode) final;
#if HAVE_OLED
	void drawPixelsForOled() override;
#endif

	deluge::static_vector<MenuItem*, n> items;
};

template <size_t n>
void Submenu<n>::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.currentSubmenuItem = items.begin();
	soundEditor.menuCurrentScroll = 0;
	soundEditor.currentMultiRange = nullptr;
	if (navigatedBackwardFrom != nullptr) {
		while (*soundEditor.currentSubmenuItem != navigatedBackwardFrom) {
			if (!*soundEditor.currentSubmenuItem) { // If desired item not found
				soundEditor.currentSubmenuItem = items.begin();
				break;
			}
			soundEditor.currentSubmenuItem++;
		}
	}
	while (!(*soundEditor.currentSubmenuItem)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex)) {
		soundEditor.currentSubmenuItem++;
		if (*soundEditor.currentSubmenuItem == nullptr) { // Not sure we need this since we don't wrap submenu items?
			soundEditor.currentSubmenuItem = items.begin();
		}
	}
#if !HAVE_OLED
	updateDisplay();
#endif
}

template <size_t n>
void Submenu<n>::updateDisplay() {
#if HAVE_OLED
	renderUIsForOled();
#else
	(*soundEditor.currentSubmenuItem)->drawName();
#endif
}

#if HAVE_OLED

template <size_t n>
void Submenu<n>::drawPixelsForOled() {
	static_vector<char const*, OLED_MENU_NUM_OPTIONS_VISIBLE> itemNames = {};

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
			if (thisSubmenuItem == this->items.begin()) { // Back at start, so render and return.
				drawItemsForOled(itemNames, selectedRow);
				return;
			}
			thisSubmenuItem--;

			if ((*thisSubmenuItem)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex)) {
				break;
			}
		}

		itemNames[i] = (*thisSubmenuItem)->getName();
	}

	drawItemsForOled(itemNames, selectedRow);
}
#endif

template <size_t n>
void Submenu<n>::selectEncoderAction(int offset) {

	MenuItem** thisSubmenuItem = soundEditor.currentSubmenuItem;

	do {
		if (offset >= 0) {
			thisSubmenuItem++;
			if (*thisSubmenuItem == nullptr) {
#if HAVE_OLED
				return;
#else
				thisSubmenuItem = items.begin();
#endif
			}
		}
		else {
			if (thisSubmenuItem == items.begin()) {
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

template <size_t n>
MenuItem* Submenu<n>::selectButtonPress() {
	return *soundEditor.currentSubmenuItem;
}

template <size_t n>
void Submenu<n>::unlearnAction() {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*soundEditor.currentSubmenuItem)->unlearnAction();
	}
}

template <size_t n>
bool Submenu<n>::allowsLearnMode() {
	if (soundEditor.getCurrentMenuItem() == this) {
		return (*soundEditor.currentSubmenuItem)->allowsLearnMode();
	}
	return false;
}

template <size_t n>
void Submenu<n>::learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*soundEditor.currentSubmenuItem)->learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	}
}

template <size_t n>
bool Submenu<n>::learnNoteOn(MIDIDevice* fromDevice, int channel, int noteCode) {
	if (soundEditor.getCurrentMenuItem() == this) {
		return (*soundEditor.currentSubmenuItem)->learnNoteOn(fromDevice, channel, noteCode);
	}
	return false;
}

} // namespace deluge::gui::menu_item
