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
		submenu->beginSession();
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

void HorizontalMenuCombined::renderMenuItems(std::span<MenuItem*> items, const MenuItem*) {
	current_submenu_->renderMenuItems(items, *current_item_);
}

ActionResult HorizontalMenuCombined::selectMenuItem(std::span<MenuItem*> pageItems, const MenuItem* previous,
                                                    int32_t selectedColumn) {
	const auto result = current_submenu_->selectMenuItem(pageItems, previous, selectedColumn);
	current_item_ = current_submenu_->current_item_;
	return result;
}

HorizontalMenu::Paging HorizontalMenuCombined::splitMenuItemsByPages(std::span<MenuItem*>, const MenuItem*) {
	// Combine all pages of all submenus into one list
	std::vector<Page> pages{};
	current_submenu_ = nullptr;

	int32_t visiblePageNumber = 0;
	int32_t selectedItemPositionOnPage = 0;
	int32_t currentPage = 0;

	for (const auto& submenu : submenus_) {
		auto submenuPaging = submenu->splitMenuItemsByPages(submenu->items, *current_item_);

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

void HorizontalMenuCombined::selectEncoderAction(int32_t offset) {
	const bool selectButtonPressed = Buttons::selectButtonPressUsedUp =
	    Buttons::isButtonPressed(hid::button::SELECT_ENC);
	if (renderingStyle() != HORIZONTAL || !selectButtonPressed) {
		return HorizontalMenu::selectEncoderAction(offset);
	}

	// Undo any acceleration: we only want it for the items, not the menu itself.
	// We only do this for horizontal menus to allow fast scrolling with shift in vertical menus.
	offset = std::clamp<int32_t>(offset, -1, 1);

	// Traverse through menu items
	int32_t submenuIndex = std::distance(submenus_.begin(), std::ranges::find(submenus_, current_submenu_));
	int32_t itemIndex = std::distance(current_submenu_->items.begin(), current_item_);

	auto moveForward = [&](int32_t& submenuIdx, int32_t& itemIdx) {
		++itemIdx;
		while (submenuIdx < static_cast<int32_t>(submenus_.size())) {
			if (itemIdx < static_cast<int32_t>(submenus_[submenuIdx]->items.size())) {
				return true;
			}
			++submenuIdx;
			itemIdx = 0;
		}
		if (wrapAround()) {
			submenuIdx = 0;
			itemIdx = 0;
			return true;
		}
		return false;
	};

	auto moveBackward = [&](int32_t& submenuIdx, int32_t& itemIdx) {
		--itemIdx;
		while (submenuIdx >= 0) {
			if (itemIdx >= 0) {
				return true;
			}
			--submenuIdx;
			if (submenuIdx >= 0) {
				itemIdx = static_cast<int32_t>(submenus_[submenuIdx]->items.size()) - 1;
			}
		}
		if (wrapAround()) {
			submenuIdx = static_cast<int32_t>(submenus_.size()) - 1;
			itemIdx = static_cast<int32_t>(submenus_[submenuIdx]->items.size()) - 1;
			return true;
		}
		return false;
	};

	if (offset > 0) {
		do {
			if (!moveForward(submenuIndex, itemIndex))
				break;
			if (auto* item = submenus_[submenuIndex]->items[itemIndex]; isItemRelevant(item)) {
				current_item_ = submenus_[submenuIndex]->items.begin() + itemIndex;
				offset--;
			}
		} while (offset > 0);
	}
	else if (offset < 0) {
		do {
			if (!moveBackward(submenuIndex, itemIndex))
				break;
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
