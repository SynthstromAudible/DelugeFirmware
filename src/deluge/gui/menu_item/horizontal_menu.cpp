#include "horizontal_menu.h"
#include "processing/sound/sound.h"
#include "submenu.h"

#include "etl/vector.h"
#include "gui/views/automation_view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "model/settings/runtime_feature_settings.h"
#include <algorithm>
#include <storage/flash_storage.h>

namespace deluge::gui::menu_item {

using namespace hid::display;

ActionResult HorizontalMenu::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	if (!on) {
		return Submenu::buttonAction(b, on, inCardRoutine);
	}

	// use SCALE / CROSS SCREEN buttons to switch between pages
	if (b == CROSS_SCREEN_EDIT || b == SCALE_MODE) {
		return switchVisiblePage(b == CROSS_SCREEN_EDIT ? 1 : -1);
	}

	// use SYNTH / KIT / MIDI / CV buttons to select menu item in the currently displayed horizontal menu page
	std::map<hid::Button, int32_t> selectMap = {{SYNTH, 0}, {KIT, 1}, {MIDI, 2}, {CV, 3}};
	if (selectMap.contains(b)) {
		return selectMenuItem(paging.getVisiblePage().items, *current_item_, selectMap[b]);
	}

	// forward other button presses to be handled by the parent
	return Submenu::buttonAction(b, on, inCardRoutine);
}

void HorizontalMenu::renderOLED() {
	if (renderingStyle() != HORIZONTAL) {
		return Submenu::renderOLED();
	}

	paging = splitMenuItemsByPages(*current_item_);

	// Lit the scale and cross-screen buttons LEDs to indicate that they can be used to switch between pages
	const auto hasPages = paging.pages.size() > 1;
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, hasPages);
	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, hasPages);

	// did the selected horizontal menu item position change?
	// if yes, update the instrument LED corresponding to that menu item position
	// store the last selected horizontal menu item position so that we don't update the LED's more than we have to
	if (const auto posOnPage = paging.selectedItemPositionOnPage; posOnPage != lastSelectedItemPosition) {
		lastSelectedItemPosition = posOnPage;
		updateSelectedMenuItemLED(posOnPage);
	}

	// Render the page counters
	if (const int32_t pagesCount = paging.pages.size(); pagesCount > 1) {
		oled_canvas::Canvas& image = OLED::main;
		constexpr int32_t y = 1 + OLED_MAIN_TOPMOST_PIXEL;
		int32_t x = OLED_MAIN_WIDTH_PIXELS - kTextSpacingX - 1;

		// Draw total count
		DEF_STACK_STRING_BUF(currentPageNum, 2);
		currentPageNum.appendInt(pagesCount);
		image.drawString(currentPageNum.c_str(), x, y, kTextSpacingX, kTextSpacingY);
		x -= kTextSpacingX - 1;

		// Draw separator line
		image.drawLine(x, y + kTextSpacingY - 2, x + 2, y + 1);
		x -= 6;

		// Draw the current one
		currentPageNum.clear();
		currentPageNum.appendInt(paging.visiblePageNumber + 1);
		image.drawString(currentPageNum.c_str(), x, y, kTextSpacingX, kTextSpacingY);
	}

	// Read all the values beforehand as content can depend on the values
	for (const auto it : items) {
		if (isItemRelevant(it)) {
			it->readCurrentValue();
		}
	}

	OLED::main.drawScreenTitle(getTitle(), false);
	renderMenuItems(paging.getVisiblePage().items, *current_item_);
	OLED::markChanged();
}

void HorizontalMenu::renderMenuItems(std::span<MenuItem*> menuItems, const MenuItem* currentItem) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr int32_t baseY = 14 + OLED_MAIN_TOPMOST_PIXEL;
	constexpr int32_t columnWidth = OLED_MAIN_WIDTH_PIXELS / 4;
	int32_t currentX = 0;

	auto it = menuItems.begin();
	for (size_t n = 0; n < menuItems.size() && it != menuItems.end(); n++) {
		MenuItem* item = *it;
		const bool isSelected = item == currentItem;

		const int32_t boxWidth = columnWidth * item->getColumnSpan();
		constexpr int32_t boxHeight = 25;
		constexpr int32_t labelHeight = kTextSpacingY;
		constexpr int32_t labelY = baseY + boxHeight - labelHeight;
		int32_t contentHeight = boxHeight;

		if (item->showColumnLabel()) {
			// Draw the label at the bottom
			renderColumnLabel(item, labelY, currentX, boxWidth, isSelected);
			contentHeight -= labelHeight;
		}

		if (layout == FIXED && !isItemRelevant(item)) {
			// Draw a dash as a value indicating that the item is disabled
			image.drawStringCentered("-", currentX, baseY + 4, kTextTitleSpacingX, kTextTitleSizeY, boxWidth);
		}
		else {
			// Draw content of the menu item
			item->renderInHorizontalMenu(currentX, boxWidth - 1, baseY, contentHeight);
		}

		// Highlight the selected item if it doesn't occupy the whole page
		if (isSelected && (menuItems.size() > 1 || menuItems[0]->getColumnSpan() < 4)) {
			if (FlashStorage::accessibilityMenuHighlighting) {
				// Highlight just by drawing a line below
				image.invertArea(currentX, boxWidth - 1, baseY + boxHeight, OLED_MAIN_VISIBLE_HEIGHT + 1);
			}
			else if (!item->showColumnLabel() || item->isSubmenu()) {
				// Highlight the whole slot if it has no label or is a submenu
				image.invertAreaRounded(currentX, boxWidth - 1, baseY, baseY + boxHeight - 1);
			}
			else {
				// Otherwise highlight only label
				image.invertAreaRounded(currentX + 1, boxWidth - 3, labelY, labelY + labelHeight - 1);
			}
		}

		currentX += boxWidth;
		it = std::next(it);
	}

	// Render placeholders for remaining slots
	for (int32_t n = currentX / (OLED_MAIN_WIDTH_PIXELS / 4); n < 4; n++) {
		const int32_t startX = n * columnWidth + 7;
		const int32_t endX = startX + 17;
		constexpr int32_t startY = baseY + 2;
		constexpr int32_t endY = OLED_MAIN_HEIGHT_PIXELS - 6;
		constexpr int32_t dotInterval = 5;

		for (int32_t x = startX + 1; x < endX; x += dotInterval) {
			image.drawPixel(x, startY + 1);
			image.drawPixel(x, endY - 1);
		}
		for (int32_t y = startY + 3; y < endY; y += dotInterval) {
			image.drawPixel(startX - 1, y);
			image.drawPixel(endX + 1, y);
		}
	}
}

void HorizontalMenu::selectEncoderAction(int32_t offset) {
	if (renderingStyle() != HORIZONTAL) {
		return Submenu::selectEncoderAction(offset);
	}

	const bool selectButtonPressed = Buttons::selectButtonPressUsedUp =
	    Buttons::isButtonPressed(hid::button::SELECT_ENC);

	if (!selectButtonPressed) {
		MenuItem* child = *current_item_;
		if (child->isSubmenu()) {
			// No action for a submenu
			return;
		}

		child->selectEncoderAction(offset);
		focusChild(child);
		displayPopup(child);

		// We don't want to return true for selectEncoderEditsInstrument(), since
		// that would trigger for scrolling in the menu as well.
		return soundEditor.markInstrumentAsEdited();
	}

	// Undo any acceleration: we only want it for the items, not the menu itself.
	// We only do this for horizontal menus to allow fast scrolling with shift in vertical menus.
	offset = std::clamp<int32_t>(offset, -1, 1);
	Submenu::selectEncoderAction(offset);
}

void HorizontalMenu::displayPopup(MenuItem* menuItem) {
	if (!menuItem->showPopup()) {
		return;
	}

	if (!menuItem->showColumnLabel() || menuItem->showValueInPopup()) {
		DEF_STACK_STRING_BUF(childValueBuf, kShortStringBufferSize);
		menuItem->getValueForPopup(childValueBuf);
		display->displayHorizontalMenuPopup(menuItem->getName(), childValueBuf.data());
	}
	else {
		display->displayHorizontalMenuPopup(menuItem->getName(), std::nullopt);
	}
}

ActionResult HorizontalMenu::switchVisiblePage(int32_t direction) {
	if (paging.pages.size() <= 1) {
		// No need to switch pages if there's only one page
		return ActionResult::DEALT_WITH;
	}

	int32_t targetPageNumber = paging.visiblePageNumber + direction;

	// Adjust the targetPageNumber to cycle through pages
	if (targetPageNumber < 0) {
		targetPageNumber = paging.pages.size() - 1;
	}
	else if (targetPageNumber >= paging.pages.size()) {
		targetPageNumber = 0;
	}

	paging.visiblePageNumber = targetPageNumber;

	// Keep the selected item position on the new page
	const auto pageItems = paging.getVisiblePage().items;
	const auto targetPosition = std::min(paging.selectedItemPositionOnPage, static_cast<int32_t>(pageItems.size() - 1));

	// Update the currently selected item
	for (int32_t pos = targetPosition; pos >= 0; pos--) {
		if (const auto item = pageItems[pos]; isItemRelevant(item)) {
			current_item_ = std::ranges::find(items, item);
			break;
		}
	}

	updateDisplay();
	updatePadLights();
	updateSelectedMenuItemLED(targetPosition);
	(*current_item_)->updateAutomationViewParameter();

	if (display->hasPopupOfType(PopupType::HORIZONTAL_MENU)) {
		display->cancelPopup();
	}

	return ActionResult::DEALT_WITH;
}

ActionResult HorizontalMenu::selectMenuItem(std::span<MenuItem*> pageItems, const MenuItem* previous,
                                            int32_t selectedColumn) {
	// Find the item you're looking for by iterating through all items on the current page
	int32_t currentColumn = 0;
	for (size_t n = 0; n < pageItems.size(); n++) {
		MenuItem* item = pageItems[n];

		// Is this item covering the selected virtual column?
		if (selectedColumn >= currentColumn && selectedColumn < currentColumn + item->getColumnSpan()) {
			if (layout == FIXED && !isItemRelevant(item)) {
				// Item is disabled, do nothing
				break;
			}

			current_item_ = std::ranges::find(items, item);
			if (*(current_item_) == previous) {
				// item is already selected
				if ((*current_item_)->isSubmenu()) {
					soundEditor.enterSubmenu(*current_item_);
				}
				else {
					displayPopup(*current_item_);
				}
				break;
			}

			// Update the currently selected item
			updateDisplay();
			updatePadLights();
			(*current_item_)->updateAutomationViewParameter();
			displayPopup(*current_item_);
			break;
		}
		currentColumn += item->getColumnSpan();
	}
	return ActionResult::DEALT_WITH;
}

HorizontalMenu::Paging HorizontalMenu::splitMenuItemsByPages(MenuItem* currentItem) {
	std::vector<Page> pages;
	std::vector<MenuItem*> currentPageItems;
	int32_t currentPageNumber = 0;
	int32_t currentPageItemsSpan = 0;

	int32_t visiblePageNumber = 0;
	int32_t selectedItemPositionOnPage = 0;

	const auto shouldIncludePage = [&](std::vector<MenuItem*>& pageItems) {
		return layout != FIXED || std::ranges::any_of(pageItems, [](MenuItem* item) { return isItemRelevant(item); });
	};

	for (auto* item : items) {
		if (layout != FIXED && !isItemRelevant(item)) {
			continue;
		}

		const int32_t itemSpan = item->getColumnSpan();
		if (currentPageItemsSpan + itemSpan > 4) {
			// Finalize the current page
			if (shouldIncludePage(currentPageItems)) {
				pages.push_back(Page{.number = currentPageNumber, .items = currentPageItems});
				++currentPageNumber;
			}

			// Start a new page
			currentPageItems.clear();
			currentPageItemsSpan = 0;
		}

		if (item == currentItem) {
			visiblePageNumber = currentPageNumber;
			selectedItemPositionOnPage = currentPageItems.size();
		}

		currentPageItems.push_back(item);
		currentPageItemsSpan += itemSpan;
	}

	if (!currentPageItems.empty() && shouldIncludePage(currentPageItems)) {
		// Finalize the last page
		pages.push_back(Page{.number = currentPageNumber, .items = currentPageItems});
	}

	return Paging{visiblePageNumber, selectedItemPositionOnPage, pages};
}

/// When updating the selected horizontal menu item, you need to refresh the lit instrument LED's
void HorizontalMenu::updateSelectedMenuItemLED(int32_t itemNumber) {
	const auto& pageItems = paging.getVisiblePage().items;
	const auto* selectedItem = pageItems[itemNumber];

	int32_t startColumn = 0;
	int32_t endColumn = 0;
	for (const auto* item : pageItems) {
		if (item == selectedItem) {
			endColumn = startColumn + item->getColumnSpan();
			break;
		}
		startColumn += item->getColumnSpan();
	}

	// Light up all buttons whose columns are covered by the selected item
	std::vector ledStates{false, false, false, false};
	if (pageItems.size() > 1 || pageItems[0]->isSubmenu() || pageItems[0]->getColumnSpan() < 4) {
		for (int32_t i = 0; i < ledStates.size(); ++i) {
			ledStates[i] = i >= startColumn && i < endColumn;
		}
	}

	indicator_leds::setLedState(IndicatorLED::SYNTH, ledStates[0]);
	indicator_leds::setLedState(IndicatorLED::KIT, ledStates[1]);
	indicator_leds::setLedState(IndicatorLED::MIDI, ledStates[2]);
	indicator_leds::setLedState(IndicatorLED::CV, ledStates[3]);
}

/// when exiting a horizontal menu, turn off the LED's and reset selected horizontal menu item position
/// so that next time you open a horizontal menu, it refreshes the LED for the selected horizontal menu item
void HorizontalMenu::endSession() {
	Submenu::endSession();

	lastSelectedItemPosition = kNoSelection;
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);

	if (display->hasPopupOfType(PopupType::HORIZONTAL_MENU)) {
		display->cancelPopup();
	}
}

Submenu::RenderingStyle HorizontalMenu::renderingStyle() const {
	if (display->haveOLED() && runtimeFeatureSettings.isOn(HorizontalMenus)) {
		return HORIZONTAL;
	}
	return VERTICAL;
}

void HorizontalMenu::renderColumnLabel(MenuItem* menuItem, int32_t labelY, int32_t slotStartX, int32_t slotWidth,
                                       bool isSelected) {
	oled_canvas::Canvas& image = OLED::main;

	DEF_STACK_STRING_BUF(label, kShortStringBufferSize);
	menuItem->getColumnLabel(label);
	label.removeSpaces();

	// If the name fits as-is, we'll squeeze it in. Otherwise, we chop off letters until
	// we have some padding between columns.
	int32_t labelWidth;
	while ((labelWidth = image.getStringWidthInPixels(label.c_str(), kTextSpacingY)) + 4 >= slotWidth) {
		label.truncate(label.size() - 1);
	}

	// Draw the label centered
	const int32_t labelStartX = slotStartX + (slotWidth - labelWidth) / 2;
	image.drawString(label.c_str(), labelStartX, labelY, kTextSpacingX, kTextSpacingY);

	if (menuItem->getColumnSpan() > 1 && !menuItem->isSubmenu() && !isSelected) {
		// Draw small lines on the left and right side if the slot is too wide
		const int32_t y = labelY + 4;
		image.drawHorizontalLine(y, slotStartX + 4, labelStartX - 4);
		image.drawHorizontalLine(y, labelStartX + labelWidth + 2, slotStartX + slotWidth - 6);
	}
}

} // namespace deluge::gui::menu_item
