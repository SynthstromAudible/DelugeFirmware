/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
	    : HorizontalMenu(l10n::String::STRING_FOR_NONE, {}), submenus_{submenus} {
		for (const auto submenu : submenus) {
			items.insert(items.end(), submenu->items.begin(), submenu->items.end());
		}
	}

	[[nodiscard]] std::string_view getTitle() const override { return current_submenu_->getTitle(); }

	void beginSession(MenuItem* navigatedBackwardFrom) override {
		navigated_backward_from = navigatedBackwardFrom;

		// Some of submenus tweak soundEditor params on the session beginning,
		// so we need to call it for each of the submenus to correctly count pages
		for (const auto submenu : submenus_) {
			submenu->beginSession();
		}
	}

	void renderMenuItems(std::span<MenuItem*> items, const MenuItem*) override {
		current_submenu_->renderMenuItems(items, *current_item_);
	}

	ActionResult selectMenuItem(std::span<MenuItem*> items, const MenuItem* previous, int32_t selectedColumn) override {
		const auto result = current_submenu_->selectMenuItem(items, previous, selectedColumn);
		current_item_ = current_submenu_->current_item_;
		return result;
	}

	Paging splitMenuItemsByPages(MenuItem*) override {
		// Combine all pages of all submenus into one list
		std::vector<Page> pages{};
		current_submenu_ = nullptr;

		int32_t visiblePageNumber = 0;
		int32_t selectedItemPositionOnPage = 0;
		int32_t currentPage = 0;

		for (const auto& submenu : submenus_) {
			auto submenuPaging = submenu->splitMenuItemsByPages(*current_item_);

			for (const auto& page : submenuPaging.pages) {
				pages.push_back({currentPage, page.items});

				if (current_submenu_ == nullptr
				    && std::ranges::any_of(page.items, [&](const auto& item) { return item == *current_item_; })) {
					current_submenu_ = submenu;
					current_submenu_->beginSession(navigated_backward_from);
					navigated_backward_from = nullptr;

					visiblePageNumber = currentPage;
					selectedItemPositionOnPage = submenuPaging.selectedItemPositionOnPage;
				}

				currentPage++;
			}
		}
		return Paging{visiblePageNumber, selectedItemPositionOnPage, pages};
	}

private:
	std::vector<HorizontalMenu*> submenus_{};
	HorizontalMenu* current_submenu_{nullptr};
	MenuItem* navigated_backward_from{nullptr};
};
} // namespace deluge::gui::menu_item
