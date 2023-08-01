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

#include "definitions.h"
#include "gui/menu_item/menu_item.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include "menu_item.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
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
	Submenu(const string& newName, MenuItem* const (&newItems)[n])
	    : MenuItem(newName), items{to_static_vector(newItems)} {}
	Submenu(const string& newName, const string& title, MenuItem* const (&newItems)[n])
	    : MenuItem(newName, title), items{to_static_vector(newItems)} {}
	Submenu(const string& newName, const string& title, std::array<MenuItem*, n> newItems)
	    : MenuItem(newName, title), items{newItems.begin(), newItems.end()} {}

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void updateDisplay();
	void selectEncoderAction(int32_t offset) final;
	MenuItem* selectButtonPress() final;
	void readValueAgain() final { updateDisplay(); }
	void unlearnAction() final;
	bool allowsLearnMode() final;
	void learnKnob(MIDIDevice* fromDevice, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final;
	bool learnNoteOn(MIDIDevice* fromDevice, int32_t channel, int32_t noteCode) final;
#if HAVE_OLED
	void drawPixelsForOled() override;
#endif

	deluge::static_vector<MenuItem*, n> items;
	typename decltype(items)::iterator current_item_;
};

template <size_t n>
void Submenu<n>::beginSession(MenuItem* navigatedBackwardFrom) {
	current_item_ = items.begin();
	soundEditor.menuCurrentScroll = 0;
	soundEditor.currentMultiRange = nullptr;
	if (navigatedBackwardFrom != nullptr) {
		for (; *current_item_ != navigatedBackwardFrom; current_item_++) {
			if (current_item_ == items.end()) { // If desired item not found
				current_item_ = items.begin();
				break;
			}
		}
	}
	while (!(*current_item_)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex)) {
		current_item_++;
		if (current_item_ == items.end()) { // Not sure we need this since we don't wrap submenu items?
			current_item_ = items.begin();
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
	(*current_item_)->drawName();
#endif
}

#if HAVE_OLED

template <size_t n>
void Submenu<n>::drawPixelsForOled() {
	int32_t selectedRow = soundEditor.menuCurrentScroll;

	// This finds the next relevant submenu item
	static_vector<string, kOLEDMenuNumOptionsVisible> nextItemNames = {};
	for (auto it = current_item_, idx = selectedRow; it != this->items.end() && idx < kOLEDMenuNumOptionsVisible;
	     it++) {
		if ((*it)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex)) {
			nextItemNames.push_back((*it)->getName());
			idx++;
		}
	}

	static_vector<string, kOLEDMenuNumOptionsVisible> prevItemNames = {};
	for (auto it = current_item_ - 1, idx = selectedRow - 1; it != this->items.begin() - 1 && idx >= 0; it--) {
		if ((*it)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex)) {
			prevItemNames.push_back((*it)->getName());
			idx--;
		}
	}
	std::reverse(prevItemNames.begin(), prevItemNames.end());

	if (!prevItemNames.empty()) {
		prevItemNames.insert(prevItemNames.end(), nextItemNames.begin(), nextItemNames.end());
		drawItemsForOled(prevItemNames, selectedRow);
	}
	else {
		drawItemsForOled(nextItemNames, selectedRow);
	}
}
#endif

template <size_t n>
void Submenu<n>::selectEncoderAction(int32_t offset) {

	auto thisSubmenuItem = current_item_;

	do {
		if (offset >= 0) {
			thisSubmenuItem++;
			if (thisSubmenuItem == items.end()) {
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
				thisSubmenuItem = &items.back();
#endif
			}
			else {
				thisSubmenuItem--;
			}
		}
	} while (!(*thisSubmenuItem)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex));

	current_item_ = thisSubmenuItem;

#if HAVE_OLED
	soundEditor.menuCurrentScroll += offset;
	if (soundEditor.menuCurrentScroll < 0) {
		soundEditor.menuCurrentScroll = 0;
	}
	else if (soundEditor.menuCurrentScroll > kOLEDMenuNumOptionsVisible - 1) {
		soundEditor.menuCurrentScroll = kOLEDMenuNumOptionsVisible - 1;
	}
#endif

	updateDisplay();
}

template <size_t n>
MenuItem* Submenu<n>::selectButtonPress() {
	return *current_item_;
}

template <size_t n>
void Submenu<n>::unlearnAction() {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*current_item_)->unlearnAction();
	}
}

template <size_t n>
bool Submenu<n>::allowsLearnMode() {
	if (soundEditor.getCurrentMenuItem() == this) {
		return (*current_item_)->allowsLearnMode();
	}
	return false;
}

template <size_t n>
void Submenu<n>::learnKnob(MIDIDevice* fromDevice, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*current_item_)->learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	}
}

template <size_t n>
bool Submenu<n>::learnNoteOn(MIDIDevice* fromDevice, int32_t channel, int32_t noteCode) {
	if (soundEditor.getCurrentMenuItem() == this) {
		return (*current_item_)->learnNoteOn(fromDevice, channel, noteCode);
	}
	return false;
}

} // namespace deluge::gui::menu_item
