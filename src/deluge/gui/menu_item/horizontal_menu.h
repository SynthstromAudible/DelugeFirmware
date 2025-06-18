#pragma once

#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/submenu.h"
#include "util/containers.h"
#include <initializer_list>
#include <span>

namespace deluge::gui::menu_item {

class HorizontalMenu : public Submenu {
public:
	enum Layout { FIXED, DYNAMIC };
	struct PageInfo {
		int32_t number;
		std::vector<MenuItem*> items;
	};
	struct Paging {
		int32_t visiblePageNumber;
		int32_t selectedItemPositionOnPage;
		std::vector<PageInfo> pages;
		PageInfo& getVisiblePage() { return pages[visiblePageNumber]; }
	};
	struct ColumnLabelPosition {
		int32_t startX;
		int32_t startY;
		int32_t width;
		int32_t height;
	};

	using Submenu::Submenu;

	HorizontalMenu(l10n::String newName, std::span<MenuItem*> newItems, Layout layout)
	    : Submenu(newName, newItems), layout(layout), paging{} {}
	HorizontalMenu(l10n::String newName, std::initializer_list<MenuItem*> newItems, Layout layout)
	    : Submenu(newName, newItems), layout(layout), paging{} {}
	HorizontalMenu(l10n::String newName, l10n::String newTitle, std::initializer_list<MenuItem*> newItems,
	               Layout layout)
	    : Submenu(newName, newTitle, newItems), layout(layout), paging{} {}

	Submenu::RenderingStyle renderingStyle() const override;
	ActionResult buttonAction(hid::Button b, bool on, bool inCardRoutine) override;
	void selectEncoderAction(int32_t offset) override;
	void renderOLED() override;
	void drawPixelsForOled() override;
	void endSession() override;

protected:
	Paging paging;
	Layout layout = DYNAMIC;
	int32_t lastSelectedItemPosition = kNoSelection;

private:
	ActionResult selectMenuItemOnVisiblePage(int32_t selectedColumn);
	ActionResult switchVisiblePage(int32_t direction);
	void updateSelectedMenuItemLED(int32_t itemNumber);
	Paging splitMenuItemsByPages() const;
	static void displayPopup(MenuItem* menuItem);
	static ColumnLabelPosition renderColumnLabel(MenuItem* menuItem, int32_t startY, int32_t slotStartX,
	                                             int32_t slotWidth);
};
} // namespace deluge::gui::menu_item
