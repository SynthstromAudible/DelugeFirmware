#pragma once

#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/submenu.h"
#include "util/containers.h"
#include <initializer_list>
#include <span>

namespace deluge::gui::menu_item {

class HorizontalMenu : public Submenu {
public:
	friend class HorizontalMenuCombined;

	enum Layout { FIXED, DYNAMIC };
	struct Page {
		mutable int32_t number;
		std::vector<MenuItem*> items;
	};
	struct Paging {
		int32_t visiblePageNumber;
		int32_t selectedItemPositionOnPage;
		std::vector<Page> pages;
		Page& getVisiblePage() { return pages[visiblePageNumber]; }
	};

	using Submenu::Submenu;

	HorizontalMenu(l10n::String newName, std::span<MenuItem*> newItems, Layout layout)
	    : Submenu(newName, newItems), layout(layout), paging{} {}
	HorizontalMenu(l10n::String newName, std::initializer_list<MenuItem*> newItems, Layout layout)
	    : Submenu(newName, newItems), layout(layout), paging{} {}
	HorizontalMenu(l10n::String newName, l10n::String newTitle, std::initializer_list<MenuItem*> newItems,
	               Layout layout)
	    : Submenu(newName, newTitle, newItems), layout(layout), paging{} {}

	RenderingStyle renderingStyle() const override;
	ActionResult buttonAction(hid::Button b, bool on, bool inCardRoutine) override;
	void selectEncoderAction(int32_t offset) override;
	void renderOLED() override;
	void drawPixelsForOled() override;
	void endSession() override;

protected:
	Paging paging;
	Layout layout = DYNAMIC;
	int32_t lastSelectedItemPosition = kNoSelection;

	ActionResult selectMenuItemOnVisiblePage(int32_t selectedColumn);
	ActionResult switchVisiblePage(int32_t direction);
	static void displayPopup(MenuItem* menuItem);
	virtual Paging splitMenuItemsByPages(std::span<MenuItem*> items);

private:
	void updateSelectedMenuItemLED(int32_t itemNumber);
	static void renderColumnLabel(MenuItem* menuItem, int32_t labelY, int32_t slotStartX, int32_t slotWidth,
	                              bool isSelected);
};
} // namespace deluge::gui::menu_item
