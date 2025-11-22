/*
 * Copyright (c) 2024 Nikodemus Siivola, Leonid Burygin
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
#include "gui/menu_item/submenu.h"
#include "util/containers.h"
#include <initializer_list>
#include <span>

namespace deluge::gui::menu_item {

class HorizontalMenu : public Submenu {
public:
	friend class HorizontalMenuGroup;

	enum Layout { FIXED, DYNAMIC };

	struct Paging {
		uint8_t visiblePageNumber;
		std::span<MenuItem*> visiblePageItems;
		uint8_t selectedItemPositionOnPage;
		uint8_t totalPages;
	};

	using Submenu::Submenu;

	HorizontalMenu(l10n::String newName, std::span<MenuItem*> newItems, Layout layout)
	    : Submenu(newName, newItems), paging{}, layout(layout) {}
	HorizontalMenu(l10n::String newName, std::initializer_list<MenuItem*> newItems, Layout layout)
	    : Submenu(newName, newItems), paging{}, layout(layout) {}
	HorizontalMenu(l10n::String newName, l10n::String newTitle, std::initializer_list<MenuItem*> newItems,
	               Layout layout)
	    : Submenu(newName, newTitle, newItems), paging{}, layout(layout) {}

	RenderingStyle renderingStyle() const override;
	ActionResult buttonAction(hid::Button b, bool on, bool inCardRoutine) override;
	void selectEncoderAction(int32_t offset) override;
	void renderOLED() override;
	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override;
	void endSession() override;

	virtual bool hasItem(const MenuItem* item);
	virtual void setCurrentItem(const MenuItem* item);
	decltype(items)& getItems() { return items; }
	MenuItem* getCurrentItem() const { return *current_item_; }

protected:
	Paging paging;
	Layout layout{DYNAMIC};
	int32_t lastSelectedItemPosition{kNoSelection};

	virtual void renderMenuItems(std::span<MenuItem*> items, const MenuItem* currentItem);
	virtual Paging& preparePaging(std::span<MenuItem*> items, const MenuItem* currentItem);
	virtual void handleInstrumentButtonPress(std::span<MenuItem*> visible_page_items, const MenuItem* previous,
	                                         int32_t pressed_button_position);
	virtual void selectMenuItem(int32_t page_number, int32_t item_pos);
	virtual void switchVisiblePage(int32_t direction);
	virtual void switchHorizontalMenu(int32_t direction, std::span<HorizontalMenu* const> chain);

private:
	void updateSelectedMenuItemLED(int32_t itemNumber) const;
	static void handleItemAction(MenuItem* menuItem);
	static void displayNotification(MenuItem* menuItem);
	void renderTitle(const Paging& paging) const;
	static void renderPageCounters(const Paging& paging);
	static void renderColumnLabel(MenuItem* menuItem, int32_t labelY, int32_t slotStartX, int32_t slotWidth,
	                              bool isSelected);

	double currentKnobSpeed{0.0};
	double calcNextKnobSpeed(int8_t offset);
};
} // namespace deluge::gui::menu_item
