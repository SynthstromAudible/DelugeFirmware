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

class HorizontalMenu : public MenuItem {
public:
	HorizontalMenu(l10n::String newName, std::initializer_list<MenuItem*> items_)
	    : MenuItem(newName), items{items_}, currentItem{items.begin()} {}
	HorizontalMenu(l10n::String newName, l10n::String newTitle, std::initializer_list<MenuItem*> items_)
	    : MenuItem(newName, newTitle), items{items_}, currentItem{items.begin()} {}
	HorizontalMenu(l10n::String newName, std::span<MenuItem*> newItems)
	    : MenuItem(newName), items{newItems.begin(), newItems.end()}, currentItem{items.begin()} {}
	HorizontalMenu(l10n::String newName, l10n::String title, std::span<MenuItem*> newItems)
	    : MenuItem(newName, title), items{newItems.begin(), newItems.end()}, currentItem{items.begin()} {}

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void updateDisplay();
	void selectEncoderAction(int32_t offset) final;
	MenuItem* selectButtonPress() final;
	void readValueAgain() final { updateDisplay(); }
	void drawPixelsForOled() final;
	bool hasInputAction() final { return true; }
	void focusChild(const MenuItem* child) override;
	bool isSubmenu() override { return true; }
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override;

private:
	deluge::vector<MenuItem*> items;
	typename decltype(items)::iterator currentItem;
};

} // namespace deluge::gui::menu_item
