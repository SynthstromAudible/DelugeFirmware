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

#include "horizontal_menu_combined.h"
#include "deluge/gui/menu_item/submenu.h"
#include <gui/menu_item/horizontal_menu.h>
#include <hid/buttons.h>
#include <ranges>
#include <string_view>

namespace deluge::gui::menu_item {

std::string_view HorizontalMenuCombined::getTitle() const {
	return current_submenu_->getTitle();
}

MenuPermission HorizontalMenuCombined::checkPermissionToBeginSession(ModControllableAudio* modControllable,
                                                                     int32_t whichThing, MultiRange** currentRange) {
	for (const auto submenu : submenus_) {
		submenu->checkPermissionToBeginSession(modControllable, whichThing, currentRange);
	}
	return MenuPermission::YES;
}

void HorizontalMenuCombined::beginSession(MenuItem* navigatedBackwardFrom) {
	navigated_backward_from = navigatedBackwardFrom;
	chain = soundEditor.getCurrentHorizontalMenusChain();

	// A submenu can modify soundEditor parameters at the beginning of a session,
	// which in their turn can affect whether an item is relevant or not.
	// So we need to begin a session for each submenu beforehand to ensure pages are counted correctly
	for (const auto submenu : submenus_) {
		submenu->beginSession(navigatedBackwardFrom);
	}
}

bool HorizontalMenuCombined::focusChild(const MenuItem* child) {
	if (child == nullptr) {
		// Select the first relevant item if the current item is not valid or relevant
		if (current_item_ == items.end() || !isItemRelevant(*current_item_)) {
			current_item_ = std::ranges::find_if(submenus_[0]->items, isItemRelevant);
		}
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

		// Child is not relevant — find first relevant among all items
		current_item_ = std::ranges::find_if(items, isItemRelevant);
		if (current_item_ != items.end()) {
			return true;
		}
	}
	return false;
}

void HorizontalMenuCombined::renderMenuItems(std::span<MenuItem*> items, const MenuItem* currentItem) {
	// Redirect rendering to the current submenu
	current_submenu_->renderMenuItems(items, currentItem);
}

void HorizontalMenuCombined::handleInstrumentButtonPress(std::span<MenuItem*> visiblePageItems,
                                                         const MenuItem* previous, int32_t pressedButtonPosition) {
	// Redirect handling to the current submenu
	current_submenu_->handleInstrumentButtonPress(visiblePageItems, previous, pressedButtonPosition);
	current_item_ = current_submenu_->current_item_;
}

void HorizontalMenuCombined::selectMenuItem(int32_t pageNumber, int32_t itemPos) {
	int32_t currentPageNumber = 0;

	for (const auto submenu : submenus_) {
		const auto submenuPagesCount = submenu->preparePaging(submenu->items, nullptr).totalPages;

		// Is the page number we want in this submenu?
		if (pageNumber <= currentPageNumber + submenuPagesCount - 1) {
			submenu->selectMenuItem(pageNumber - currentPageNumber, itemPos);
			current_item_ = submenu->current_item_;
			lastSelectedItemPosition = kNoSelection;
			return;
		}

		currentPageNumber += submenuPagesCount;
	}
}

HorizontalMenu::Paging& HorizontalMenuCombined::preparePaging(std::span<MenuItem*>, const MenuItem* currentItem) {
	static std::vector<MenuItem*> visiblePageItems;
	visiblePageItems.clear();
	visiblePageItems.reserve(4);

	uint8_t visiblePageNumber = 0;
	uint8_t selectedItemPositionOnPage = 0;
	uint8_t totalPages = 0;

	for (const auto submenu : submenus_) {
		std::optional<uint8_t> pagesCount;

		for (int32_t i = 0; i < submenu->items.size(); ++i) {
			if (const auto* item = submenu->items[i]; item != currentItem) {
				continue;
			}

			// Found current submenu
			submenu->beginSession(navigated_backward_from);

			const auto& p = submenu->preparePaging(submenu->items, currentItem);
			visiblePageItems.assign(p.visiblePageItems.begin(), p.visiblePageItems.end());
			visiblePageNumber = totalPages + p.visiblePageNumber;
			selectedItemPositionOnPage = p.selectedItemPositionOnPage;

			current_submenu_ = submenu;
			navigated_backward_from = nullptr;
			pagesCount = p.totalPages;
		}

		if (!pagesCount.has_value()) {
			pagesCount = submenu->preparePaging(submenu->items, currentItem).totalPages;
		}
		totalPages += pagesCount.value();
	}

	paging = Paging{visiblePageNumber, visiblePageItems, selectedItemPositionOnPage, totalPages};
	return paging;
}

void HorizontalMenuCombined::switchVisiblePage(int32_t direction) {
	// Try switching page within the current submenu first
	if (current_submenu_->paging.totalPages > 1) {
		// If we can stay within the current submenu, do it
		const int32_t newPage = current_submenu_->paging.visiblePageNumber + direction;
		if (newPage >= 0 && newPage < current_submenu_->paging.totalPages) {
			current_submenu_->switchVisiblePage(direction);
			current_item_ = current_submenu_->current_item_;
			lastSelectedItemPosition = kNoSelection;
			return;
		}
	}

	// Need to switch submenus - find the current submenu index
	const auto currentSubmenuIt = std::ranges::find(submenus_, current_submenu_);
	int32_t submenuIndex = std::distance(submenus_.begin(), currentSubmenuIt);

	// Move to the next / previous submenu
	submenuIndex += direction;

	// If we're outside the current combined menu, switch to the next / previous menu from the chain
	if (chain.has_value() && (submenuIndex < 0 || submenuIndex > static_cast<int32_t>(submenus_.size()) - 1)) {
		return switchHorizontalMenu(std::clamp<int32_t>(direction, -1, 1), chain.value());
	}

	// Wrap around
	const int32_t count = static_cast<int32_t>(submenus_.size());
	submenuIndex = (submenuIndex % count + count) % count;

	const auto submenu = submenus_[submenuIndex];
	const auto submenuPagesCount = submenu->preparePaging(submenu->items, nullptr).totalPages;
	if (submenuPagesCount == 0) {
		// No relevant items on the switched submenu, go to the next submenu
		return switchVisiblePage(direction >= 0 ? direction + 1 : direction - 1);
	}

	// Select an item with the same position as on the previously selected page if possible
	const auto firstOrLastPage = direction >= 0 ? 0 : submenuPagesCount - 1;
	submenu->selectMenuItem(firstOrLastPage, paging.selectedItemPositionOnPage);
	current_item_ = submenu->current_item_;
	lastSelectedItemPosition = kNoSelection;

	// Item is selected, start a session and render UI
	submenu->beginSession(nullptr);
	updateDisplay();
	updatePadLights();
	(*current_item_)->updateAutomationViewParameter();

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
