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

namespace deluge::gui::menu_item {

ActionResult HorizontalMenu::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	if (!on) {
		return Submenu::buttonAction(b, on, inCardRoutine);
	}

	// use SYNTH / KIT / MIDI / CV buttons to select menu item in the currently displayed horizontal menu page
	// use SCALE / CROSS SCREEN buttons to switch between pages
	switch (b) {
	case SYNTH:
		return selectMenuItemOnVisiblePage(0);
	case KIT:
		return selectMenuItemOnVisiblePage(1);
	case MIDI:
		return selectMenuItemOnVisiblePage(2);
	case CV:
		return selectMenuItemOnVisiblePage(3);
	case CROSS_SCREEN_EDIT:
	case SCALE_MODE:
		return switchVisiblePage(b == CROSS_SCREEN_EDIT ? 1 : -1);
	}

	// forward other button presses to be handled by the parent
	return Submenu::buttonAction(b, on, inCardRoutine);
}

void HorizontalMenu::renderOLED() {
	// need to draw the content first because the title can depend on the content.
	// example: mod-fx menu where the second page title is depending on the selected mod-fx type
	drawPixelsForOled();
	hid::display::OLED::main.drawScreenTitle(getTitle());
	hid::display::OLED::markChanged();
}

void HorizontalMenu::drawPixelsForOled() {
	if (renderingStyle() != HORIZONTAL) {
		return Submenu::drawPixelsForOled();
	}

	hid::display::oled_canvas::Canvas& image = hid::display::OLED::main;

	paging = splitMenuItemsByPages();

	// Lit the scale and cross-screen buttons LEDs to indicate that they can be used to switch between pages
	const auto hasPages = paging.pages.size() > 1;
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, hasPages);
	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, hasPages);

	// did the selected horizontal menu item position change?
	// if yes, update the instrument LED corresponding to that menu item position
	// store the last selected horizontal menu item position so that we don't update the LED's more than we have to
	auto posOnPage = paging.selectedItemPositionOnPage;
	if (posOnPage != lastSelectedItemPosition) {
		lastSelectedItemPosition = posOnPage;
		updateSelectedMenuItemLED(posOnPage);
	}

	constexpr int32_t baseY = 14 + OLED_MAIN_TOPMOST_PIXEL;
	constexpr int32_t boxHeight = OLED_MAIN_VISIBLE_HEIGHT - baseY;
	int32_t currentX = 0;

	const auto menuStyle = runtimeFeatureSettings.get(HorizontalMenuStyle);
	auto& [pageNumber, pageItems] = paging.getVisiblePage();
	auto it = pageItems.begin();

	// Render the page
	for (size_t n = 0; n < pageItems.size() && it != pageItems.end(); n++) {
		MenuItem* item = *it;

		const int32_t boxWidth = OLED_MAIN_WIDTH_PIXELS / (4 / item->getColumnSpan());
		int32_t contentY = baseY;
		int32_t contentHeight = boxHeight;
		std::optional<ColumnLabelPosition> labelPos;

		if (item->showColumnLabel()) {
			// Draw the label at the bottom if the graphics enabled, otherwise draw the label at the top
			const int32_t labelY = menuStyle == Graphical ? OLED_MAIN_HEIGHT_PIXELS - kTextSpacingY - 4 : baseY;
			labelPos = renderColumnLabel(item, labelY, currentX, boxWidth);

			contentY = menuStyle == Graphical ? baseY : baseY + kTextSpacingY;
			contentHeight = boxHeight - labelPos->height;
		}

		if (layout == FIXED && !isItemRelevant(item)) {
			// Draw a dash as a value indicating that the item is disabled
			image.drawStringCentered("-", currentX, contentY + 4, kTextTitleSpacingX, kTextTitleSizeY, boxWidth);
		}
		else {
			// Draw content of the menu item
			item->readCurrentValue();
			item->renderInHorizontalMenu(currentX + 1, boxWidth, contentY, contentHeight);
		}

		// Draw a dotted separator at the end of the menu item
		// Only if:
		// - this item is not the selected item or its immediate neighbors
		// - not the closest item to the right side
		// - we have numeric style enabled
		if (menuStyle == Numeric && n != posOnPage - 1 && n != posOnPage
		    && currentX + boxWidth != OLED_MAIN_WIDTH_PIXELS) {
			const int32_t lineX = currentX + boxWidth - 1;
			for (int32_t y = baseY; y < baseY + boxHeight + 2; y += 2) {
				image.drawPixel(lineX, y);
			}
		}

		// Highlight the selected item if it doesn't occupy the whole page
		if (n == posOnPage && (pageItems.size() > 1 || pageItems[0]->getColumnSpan() < 4)) {
			if (menuStyle == Numeric || !labelPos.has_value() || item->isSubmenu()) {
				// highlight the whole slot
				image.invertArea(currentX, boxWidth, baseY, baseY + boxHeight + 1);
			}
			else {
				// highlight only label
				image.invertArea(labelPos->startX - 2, labelPos->width + 3, labelPos->startY,
				                 labelPos->startY + labelPos->height - 1);
			}
		}

		currentX += boxWidth;
		it = std::next(it);
	}

	// Render the page counters
	if (paging.pages.size() > 1) {
		int32_t extraY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 0 : 1;
		int32_t pageY = extraY + OLED_MAIN_TOPMOST_PIXEL;
		int32_t endX = OLED_MAIN_WIDTH_PIXELS;

		for (int32_t p = paging.pages.size(); p > 0; p--) {
			DEF_STACK_STRING_BUF(pageNum, 2);
			pageNum.appendInt(p);
			int32_t w = image.getStringWidthInPixels(pageNum.c_str(), kTextSpacingY);
			image.drawString(pageNum.c_str(), endX - w, pageY, kTextSpacingX, kTextSpacingY);
			endX -= w + 1;
			if (p - 1 == pageNumber) {
				image.invertArea(endX, w + 1, pageY, pageY + kTextSpacingY);
			}
		}
	}
}

void HorizontalMenu::selectEncoderAction(int32_t offset) {
	if (renderingStyle() == HORIZONTAL) {
		MenuItem* child = *current_item_;
		if (child->isSubmenu()) {
			return;
		}

		bool selectButtonPressed = Buttons::selectButtonPressUsedUp = Buttons::isButtonPressed(hid::button::SELECT_ENC);
		if (!selectButtonPressed) {
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
	}

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
	current_item_ = std::ranges::find(items, pageItems[targetPosition]);
	updateDisplay();
	updatePadLights();
	updateSelectedMenuItemLED(targetPosition);
	(*current_item_)->updateAutomationViewParameter();

	return ActionResult::DEALT_WITH;
}

/// Selects the menu item covering the given virtual column on the visible page
ActionResult HorizontalMenu::selectMenuItemOnVisiblePage(int32_t selectedColumn) {
	auto& pageItems = paging.getVisiblePage().items;

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

			// Update the currently selected item
			const auto previous_item = current_item_;
			current_item_ = std::ranges::find(items, item);

			if (current_item_ == previous_item) {
				// item is already selected
				if ((*current_item_)->isSubmenu()) {
					soundEditor.enterSubmenu(*current_item_);
				}
				else {
					displayPopup(*current_item_);
				}
				break;
			}

			updateDisplay();
			updatePadLights();
			(*current_item_)->updateAutomationViewParameter();
			break;
		}
		currentColumn += item->getColumnSpan();
	}
	return ActionResult::DEALT_WITH;
}

HorizontalMenu::Paging HorizontalMenu::splitMenuItemsByPages() const {
	std::vector<PageInfo> pages;
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
				pages.push_back(PageInfo{.number = currentPageNumber, .items = currentPageItems});
				++currentPageNumber;
			}

			// Start a new page
			currentPageItems.clear();
			currentPageItemsSpan = 0;
		}

		if (item == *current_item_) {
			visiblePageNumber = currentPageNumber;
			selectedItemPositionOnPage = currentPageItems.size();
		}

		currentPageItems.push_back(item);
		currentPageItemsSpan += itemSpan;
	}

	if (!currentPageItems.empty() && shouldIncludePage(currentPageItems)) {
		// Finalize the last page
		pages.push_back(PageInfo{.number = currentPageNumber, .items = currentPageItems});
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

HorizontalMenu::ColumnLabelPosition HorizontalMenu::renderColumnLabel(MenuItem* menuItem, int32_t startY,
                                                                      int32_t slotStartX, int32_t slotWidth) {
	hid::display::oled_canvas::Canvas& image = hid::display::OLED::main;

	const bool useSmallFont = runtimeFeatureSettings.get(HorizontalMenusSmallFontForLabels) == On;

	DEF_STACK_STRING_BUF(label, kShortStringBufferSize);
	menuItem->getColumnLabel(label, useSmallFont);

	// Remove any spaces
	label.removeSpaces();

	const int32_t textSpacingX = useSmallFont ? kTextSmallSpacingX : kTextSpacingX;
	const int32_t textSpacingY = useSmallFont ? kTextSmallSizeY : kTextSpacingY;
	const int32_t topPadding = useSmallFont ? 3 : 1;

	int32_t labelWidth = image.getStringWidthInPixels(label.c_str(), textSpacingY);
	// If the name fits as-is, we'll squeeze it in. Otherwise, we chop off letters until
	// we have some padding between columns.
	if (labelWidth >= slotWidth - 2) {
		do {
			label.truncate(label.size() - 1);
		} while ((labelWidth = image.getStringWidthInPixels(label.c_str(), textSpacingY)) + 4 >= slotWidth);
	}

	// center the label
	const int32_t labelStartX = (slotStartX + (slotWidth - labelWidth) / 2);
	const int32_t labelStartY = startY + topPadding;

	hid::display::OLED::main.drawString(label.c_str(), labelStartX, labelStartY, textSpacingX, textSpacingY, 0,
	                                    labelStartX + slotWidth - textSpacingX);

	return {labelStartX, labelStartY, labelWidth, textSpacingY};
}

} // namespace deluge::gui::menu_item
