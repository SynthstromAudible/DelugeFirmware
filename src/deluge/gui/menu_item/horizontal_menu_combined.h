/*
 * Copyright (c) 2025 Leonid Burygin
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

#include "deluge/gui/menu_item/submenu.h"
#include <gui/menu_item/horizontal_menu.h>
#include <string_view>

namespace deluge::gui::menu_item {

// A class to combine multiple Horizontal menus into a single long Horizontal menu with paging
class HorizontalMenuCombined final : public HorizontalMenu {
public:
	HorizontalMenuCombined(std::initializer_list<HorizontalMenu*> submenus)
	    : HorizontalMenu(l10n::String::STRING_FOR_NONE, {}), submenus_{submenus} {}

	[[nodiscard]] std::string_view getTitle() const override;
	bool focusChild(const MenuItem* child) override;
	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             MultiRange** currentRange) override;
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void selectEncoderAction(int32_t offset) override;
	void renderMenuItems(std::span<MenuItem*> items, const MenuItem* currentItem) override;
	void selectMenuItem(std::span<MenuItem*> pageItems, const MenuItem* previous, int32_t selectedColumn) override;
	Paging& preparePaging(std::span<MenuItem*> items, const MenuItem*) override;
	void switchVisiblePage(int32_t direction) override;

private:
	std::vector<HorizontalMenu*> submenus_{};
	HorizontalMenu* current_submenu_{nullptr};
	MenuItem* navigated_backward_from{nullptr};
};
} // namespace deluge::gui::menu_item
