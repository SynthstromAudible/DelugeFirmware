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
#include "gui/ui/sound_editor.h"
#include "menu_item.h"
#include "util/containers.h"
#include <initializer_list>
#include <span>

namespace deluge::gui::menu_item {

class Submenu : public MenuItem {
public:
	Submenu(l10n::String newName, std::initializer_list<MenuItem*> newItems) : MenuItem(newName), items{newItems} {}
	Submenu(l10n::String newName, std::span<MenuItem*> newItems)
	    : MenuItem(newName), items{newItems.begin(), newItems.end()} {}
	Submenu(l10n::String newName, l10n::String title, std::initializer_list<MenuItem*> newItems)
	    : MenuItem(newName, title), items{newItems} {}

	Submenu(l10n::String newName, l10n::String title, std::span<MenuItem*> newItems)
	    : MenuItem(newName, title), items{newItems.begin(), newItems.end()} {}

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void updateDisplay();
	void selectEncoderAction(int32_t offset) final;
	MenuItem* selectButtonPress() final;
	void readValueAgain() final { updateDisplay(); }
	void unlearnAction() final;
	bool allowsLearnMode() final;
	void learnKnob(MIDIDevice* fromDevice, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final;
	void learnProgramChange(MIDIDevice* fromDevice, int32_t channel, int32_t programNumber);
	bool learnNoteOn(MIDIDevice* fromDevice, int32_t channel, int32_t noteCode) final;
	void drawPixelsForOled() override;

	deluge::vector<MenuItem*> items;
	typename decltype(items)::iterator current_item_;
};

} // namespace deluge::gui::menu_item
