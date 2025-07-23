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

#include "horizontal_menu_combined.h"
#include "deluge/gui/menu_item/submenu.h"
#include <gui/menu_item/horizontal_menu.h>
#include <hid/buttons.h>
#include <string_view>

namespace deluge::gui::menu_item {

std::string_view HorizontalMenuCombined::getTitle() const {
	return current_submenu_->getTitle();
}

void HorizontalMenuCombined::beginSession(MenuItem* navigatedBackwardFrom) {
	navigated_backward_from = navigatedBackwardFrom;

	// Some submenus modify soundEditor parameters at the start of the session,
	// so we need to call it for each submenu to ensure pages are counted correctly
	for (const auto submenu : submenus_) {
		submenu->beginSession(navigatedBackwardFrom);
	}
}

bool HorizontalMenuCombined::focusChild(const MenuItem* child) {
	if (child == nullptr) {
		return true;
	}

	// Try to select the specified child if it's among the submenu items
	for (auto* submenu : submenus_) {
		auto& items = submenu->items;

		auto it = std::ranges::find(items, child);
		if (it == items.end())
			continue;

		// Found the child
		if (isItemRelevant(*it)) {
			current_item_ = it;
			return true;
		}

		// Child is not relevant — search left for the closest relevant item
		for (int32_t i = std::distance(items.begin(), it); i >= 0; --i) {
			if (isItemRelevant(items[i])) {
				current_item_ = items.begin() + i;
				return true;
			}
		}
	}
	return true;
}

void HorizontalMenuCombined::renderMenuItems(std::span<MenuItem*> items, const MenuItem* currentItem) {
	// Redirect rendering to the current submenu
	current_submenu_->renderMenuItems(items, currentItem);
}

void HorizontalMenuCombined::selectMenuItem(std::span<MenuItem*> pageItems, const MenuItem* previous,
                                            int32_t selectedColumn) {
	// Redirect selecting to the current submenu
	current_submenu_->selectMenuItem(pageItems, previous, selectedColumn);
	current_item_ = current_submenu_->current_item_;
}

HorizontalMenu::Paging HorizontalMenuCombined::preparePaging(std::span<MenuItem*>, const MenuItem* currentItem) {
	static std::vector<MenuItem*> visiblePageItems;
	visiblePageItems.clear();
	visiblePageItems.reserve(4);

	uint8_t visiblePageNumber = 0;
	uint8_t selectedItemPositionOnPage = 0;
	uint8_t totalPages = 0;

	for (const auto submenu : submenus_) {
		const auto submenuPaging = submenu->preparePaging(submenu->items, currentItem);

		for (const auto* item : submenu->items) {
			if (item == currentItem) {
				// Found current submenu
				visiblePageItems.assign(submenuPaging.visiblePageItems.begin(), submenuPaging.visiblePageItems.end());
				visiblePageNumber = totalPages + submenuPaging.visiblePageNumber;
				selectedItemPositionOnPage = submenuPaging.selectedItemPositionOnPage;

				current_submenu_ = submenu;
				current_submenu_->paging = submenuPaging;
				current_submenu_->beginSession(navigated_backward_from);
				navigated_backward_from = nullptr;
			}
		}

		totalPages += submenuPaging.totalPages;
	}

	return Paging{visiblePageNumber, visiblePageItems, selectedItemPositionOnPage, totalPages};
}

void HorizontalMenuCombined::switchVisiblePage(int32_t direction) {
	// Try switching page within the current submenu first
	if (current_submenu_->paging.totalPages > 1) {
		// If we can stay within the current submenu, do it
		const int32_t newPage = current_submenu_->paging.visiblePageNumber + direction;
		if (newPage >= 0 && newPage < current_submenu_->paging.totalPages) {
			current_submenu_->switchVisiblePage(direction);
			current_item_ = current_submenu_->current_item_;
			return;
		}
	}

	// Need to switch submenus - find the current submenu index
	const auto currentSubmenuIt = std::ranges::find(submenus_, current_submenu_);
	int32_t submenuIndex = std::distance(submenus_.begin(), currentSubmenuIt);

	// Move to the next / previous submenu
	submenuIndex += direction;

	// Wrap around
	if (submenuIndex < 0) {
		submenuIndex = submenus_.size() - 1;
	}
	else if (submenuIndex >= static_cast<int32_t>(submenus_.size())) {
		submenuIndex = 0;
	}

	// Find the item we're looking for on the switched submenu
	const auto submenu = submenus_[submenuIndex];
	const auto firstOrLastItem = submenu->items[direction >= 0 ? 0 : submenu->items.size() - 1];
	const auto firstOrLastPageItems = submenu->preparePaging(submenu->items, firstOrLastItem).visiblePageItems;

	if (firstOrLastPageItems.size() == 0) {
		// No relevant items on the switched submenu, go to the next submenu
		return switchVisiblePage(direction >= 0 ? direction + 1 : direction - 1);
	}

	int32_t currentPos = -1;
	for (const auto& item : firstOrLastPageItems) {
		// Select an item with the same position as on the previous selected page
		// If the item is not relevant, select the closest item instead
		if (isItemRelevant(item)) {
			current_item_ = std::ranges::find(submenu->items, item);
			if (++currentPos >= paging.selectedItemPositionOnPage) {
				break;
			}
		}
	}

	updateDisplay();
	updatePadLights();
	(*current_item_)->updateAutomationViewParameter();
	lastSelectedItemPosition = kNoSelection;

	if (display->hasPopupOfType(PopupType::NOTIFICATION)) {
		display->cancelPopup();
	}
}

void HorizontalMenuCombined::selectEncoderAction(int32_t offset) {
	const bool selectButtonPressed = Buttons::selectButtonPressUsedUp =
	    Buttons::isButtonPressed(hid::button::SELECT_ENC);

	if (renderingStyle() != HORIZONTAL || !selectButtonPressed) {
		return HorizontalMenu::selectEncoderAction(offset);
	}

	// Traverse through menu items
	int32_t submenuIndex = std::distance(submenus_.begin(), std::ranges::find(submenus_, current_submenu_));
	int32_t itemIndex = std::distance(current_submenu_->items.begin(), current_item_);

	auto moveForward = [&](int32_t& submenuIdx, int32_t& itemIdx) {
		++itemIdx;
		while (submenuIdx < static_cast<int32_t>(submenus_.size())) {
			if (itemIdx < static_cast<int32_t>(submenus_[submenuIdx]->items.size())) {
				return;
			}
			++submenuIdx;
			itemIdx = 0;
		}
		submenuIdx = 0;
		itemIdx = 0;
	};

	auto moveBackward = [&](int32_t& submenuIdx, int32_t& itemIdx) {
		--itemIdx;
		while (submenuIdx >= 0) {
			if (itemIdx >= 0) {
				return;
			}
			--submenuIdx;
			if (submenuIdx >= 0) {
				itemIdx = static_cast<int32_t>(submenus_[submenuIdx]->items.size()) - 1;
			}
		}
		submenuIdx = static_cast<int32_t>(submenus_.size()) - 1;
		itemIdx = static_cast<int32_t>(submenus_[submenuIdx]->items.size()) - 1;
	};

	if (offset > 0) {
		do {
			moveForward(submenuIndex, itemIndex);
			if (auto* item = submenus_[submenuIndex]->items[itemIndex]; isItemRelevant(item)) {
				current_item_ = submenus_[submenuIndex]->items.begin() + itemIndex;
				offset--;
			}
		} while (offset > 0);
	}
	else if (offset < 0) {
		do {
			moveBackward(submenuIndex, itemIndex);
			if (auto* item = submenus_[submenuIndex]->items[itemIndex]; isItemRelevant(item)) {
				current_item_ = submenus_[submenuIndex]->items.begin() + itemIndex;
				offset++;
			}
		} while (offset < 0);
	}

	updateDisplay();
	updatePadLights();
	(*current_item_)->updateAutomationViewParameter();
}

} // namespace deluge::gui::menu_item
