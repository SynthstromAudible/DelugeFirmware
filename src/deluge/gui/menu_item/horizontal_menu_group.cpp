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

#include "horizontal_menu_group.h"
#include "deluge/gui/menu_item/submenu.h"
#include "gui/menu_item/horizontal_menu.h"
#include "hid/buttons.h"
#include <ranges>
#include <string_view>

namespace deluge::gui::menu_item {

std::string_view HorizontalMenuGroup::getTitle() const {
	return current_menu_->getTitle();
}

void HorizontalMenuGroup::beginSession(MenuItem* navigatedBackwardFrom) {
	HorizontalMenu::beginSession(navigatedBackwardFrom);
	navigated_backward_from = navigatedBackwardFrom;
	lastSelectedItemPosition = kNoSelection;

	for (const auto menu : menus_) {
		menu->parent = this;
		for (const auto it : menu->items) {
			it->parent = menu;
		}
	}
}

void HorizontalMenuGroup::endSession() {
	HorizontalMenu::endSession();

	for (const auto menu : menus_) {
		menu->parent = nullptr;
		for (const auto it : menu->items) {
			it->parent = nullptr;
		}
	}
}

bool HorizontalMenuGroup::focusChild(const MenuItem* child) {
	if (child == nullptr) {
		// Select the first relevant item if the current item is not valid or relevant
		if (current_item_ == items.end() || !isItemRelevant(*current_item_)) {
			current_item_ = std::ranges::find_if(menus_[0]->items, isItemRelevant);
		}
		return true;
	}

	// Try to select the specified child if it's among the menu items
	for (auto* menu : menus_) {
		auto& items = menu->items;

		auto it = std::ranges::find(items, child);
		if (it == items.end())
			continue;

		// Found the child
		if (isItemRelevant(*it)) {
			current_item_ = it;
			return true;
		}

		// Child is not relevant — find first relevant among all items
		it = std::ranges::find_if(items, isItemRelevant);
		if (it != items.end()) {
			current_item_ = it;
			return true;
		}
	}
	return false;
}

void HorizontalMenuGroup::renderMenuItems(std::span<MenuItem*> items, const MenuItem* currentItem) {
	// Redirect rendering to the current menu
	current_menu_->renderMenuItems(items, currentItem);
}

void HorizontalMenuGroup::handleInstrumentButtonPress(std::span<MenuItem*> visiblePageItems, const MenuItem* previous,
                                                      int32_t pressedButtonPosition) {
	// Redirect handling to the current menu
	current_menu_->handleInstrumentButtonPress(visiblePageItems, previous, pressedButtonPosition);
	current_item_ = current_menu_->current_item_;
}

void HorizontalMenuGroup::selectMenuItem(int32_t pageNumber, int32_t itemPos) {
	int32_t currentPageNumber = 0;

	for (const auto menu : menus_) {
		const auto pagesCount = menu->preparePaging(menu->items, nullptr).totalPages;

		if (pageNumber <= currentPageNumber + pagesCount - 1) {
			menu->selectMenuItem(pageNumber - currentPageNumber, itemPos);
			current_item_ = menu->current_item_;
			lastSelectedItemPosition = kNoSelection;
			return;
		}

		currentPageNumber += pagesCount;
	}
}

HorizontalMenu::Paging& HorizontalMenuGroup::preparePaging(std::span<MenuItem*>, const MenuItem* currentItem) {
	static std::vector<MenuItem*> visiblePageItems;
	visiblePageItems.clear();
	visiblePageItems.reserve(4);

	uint8_t visiblePageNumber = 0;
	uint8_t selectedItemPositionOnPage = 0;
	uint8_t totalPages = 0;

	for (const auto menu : menus_) {
		std::optional<uint8_t> pagesCount;

		for (int32_t i = 0; i < menu->items.size(); ++i) {
			if (const auto* item = menu->items[i]; item != currentItem) {
				continue;
			}

			// Found current menu
			menu->beginSession(navigated_backward_from);

			const auto& p = menu->preparePaging(menu->items, currentItem);
			visiblePageItems.assign(p.visiblePageItems.begin(), p.visiblePageItems.end());
			visiblePageNumber = totalPages + p.visiblePageNumber;
			selectedItemPositionOnPage = p.selectedItemPositionOnPage;

			current_menu_ = menu;
			navigated_backward_from = nullptr;
			pagesCount = p.totalPages;
		}

		if (!pagesCount.has_value()) {
			pagesCount = menu->preparePaging(menu->items, currentItem).totalPages;
		}
		totalPages += pagesCount.value();
	}

	paging = Paging{visiblePageNumber, visiblePageItems, selectedItemPositionOnPage, totalPages};
	return paging;
}

void HorizontalMenuGroup::switchVisiblePage(int32_t direction) {
	// Try switching page within the current menu first
	if (current_menu_->paging.totalPages > 1) {
		// If we can stay within the current menu, do it
		const int32_t newPage = current_menu_->paging.visiblePageNumber + direction;
		if (newPage >= 0 && newPage < current_menu_->paging.totalPages) {
			current_menu_->switchVisiblePage(direction);
			current_item_ = current_menu_->current_item_;
			lastSelectedItemPosition = kNoSelection;
			return;
		}
	}

	// Need to switch menus - find the current menu index
	const auto currentMenuIt = std::ranges::find(menus_, current_menu_);
	int32_t menuIndex = std::distance(menus_.begin(), currentMenuIt);

	// Move to the next / previous menu
	menuIndex += direction;

	// Wrap around
	const int32_t count = static_cast<int32_t>(menus_.size());
	menuIndex = (menuIndex % count + count) % count;

	const auto newMenu = menus_[menuIndex];
	const auto pagesCount = newMenu->preparePaging(newMenu->items, nullptr).totalPages;
	if (pagesCount == 0) {
		// No relevant items on the switched menu, go to the next menu
		return switchVisiblePage(direction >= 0 ? ++direction : --direction);
	}

	// Select an item with the same position as on the previously selected page if possible
	const auto firstOrLastPage = direction >= 0 ? 0 : pagesCount - 1;
	newMenu->beginSession(nullptr);
	newMenu->selectMenuItem(firstOrLastPage, paging.selectedItemPositionOnPage);
	current_item_ = newMenu->current_item_;
	lastSelectedItemPosition = kNoSelection;

	// Update UI
	updateDisplay();
	updatePadLights();

	if (display->hasPopupOfType(PopupType::NOTIFICATION)) {
		display->cancelPopup();
	}
}

bool HorizontalMenuGroup::hasItem(const MenuItem* item) {
	return std::ranges::any_of(menus_, [&](auto menu) { return menu->hasItem(item); });
}

void HorizontalMenuGroup::setCurrentItem(const MenuItem* item) {
	for (auto* menu : menus_) {
		current_item_ = std::ranges::find(menu->items, item);
		if (current_item_ != menu->items.end()) {
			return;
		}
	}
}

} // namespace deluge::gui::menu_item
