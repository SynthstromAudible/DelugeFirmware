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
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void endSession() override;

protected:
	Paging paging;
	Layout layout = DYNAMIC;
	int32_t lastSelectedItemPosition = kNoSelection;

	virtual void renderMenuItems(std::span<MenuItem*> items, const MenuItem* currentItem);
	virtual Paging preparePaging(std::span<MenuItem*> items, const MenuItem* currentItem);
	virtual void selectMenuItem(std::span<MenuItem*> pageItems, const MenuItem* previous, int32_t selectedColumn);
	virtual void switchVisiblePage(int32_t direction);

private:
	void updateSelectedMenuItemLED(int32_t itemNumber) const;
	static void displayNotification(MenuItem* menuItem);
	static void renderPageCounters(const Paging& paging);
	static void renderColumnLabel(MenuItem* menuItem, int32_t labelY, int32_t slotStartX, int32_t slotWidth,
	                              bool isSelected);

	double currentKnobSpeed{0.0};
	double calcNextKnobSpeed(int8_t offset);
};
} // namespace deluge::gui::menu_item
