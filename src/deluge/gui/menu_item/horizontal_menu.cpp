/*
 * Copyright (c) 2024 Nikodemus Siivola / Leonid Burygin
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

#include "horizontal_menu.h"

#include "multi_range.h"
#include "processing/sound/sound.h"
#include "submenu.h"

#include "etl/vector.h"
#include "gui/views/automation_view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "model/settings/runtime_feature_settings.h"
#include <algorithm>
#include <hid/buttons.h>
#include <ranges>
#include <storage/flash_storage.h>

namespace deluge::gui::menu_item {

using namespace hid::display;

void HorizontalMenu::beginSession(MenuItem* navigatedBackwardFrom) {
	Submenu::beginSession(navigatedBackwardFrom);

	for (const auto it : items) {
		it->parent = this;
		// Important initialization stuff may happen in this check
		// E.g. the patch cable strength menu sets the current source there
		it->checkPermissionToBeginSession(soundEditor.currentModControllable, soundEditor.currentSourceIndex,
		                                  &soundEditor.currentMultiRange);
	}

	// Workaround: checkPermissionToBeginSession could cause unnecessary popups
	if (display->hasPopupOfType(PopupType::GENERAL)) {
		display->cancelPopup();
	}
}

ActionResult HorizontalMenu::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	if (!on) {
		return Submenu::buttonAction(b, on, inCardRoutine);
	}

	// use SCALE / CROSS SCREEN buttons to switch between pages
	if (b == CROSS_SCREEN_EDIT || b == SCALE_MODE) {
		switchVisiblePage(b == CROSS_SCREEN_EDIT ? 1 : -1);
		return ActionResult::DEALT_WITH;
	}

	// use SYNTH / KIT / MIDI / CV buttons to select menu item in the currently displayed horizontal menu page
	static std::map<hid::Button, int32_t> selectMap = {{SYNTH, 0}, {KIT, 1}, {MIDI, 2}, {CV, 3}};
	if (selectMap.contains(b)) {
		selectMenuItem(paging.visiblePageItems, *current_item_, selectMap[b]);
		return ActionResult::DEALT_WITH;
	}

	// forward other button presses to be handled by the parent
	return Submenu::buttonAction(b, on, inCardRoutine);
}

void HorizontalMenu::renderOLED() {
	if (renderingStyle() != HORIZONTAL) {
		return Submenu::renderOLED();
	}

	preparePaging(items, *current_item_);

	// Lit the scale and cross-screen buttons LEDs to indicate that they can be used to switch between pages
	const auto hasPages = paging.totalPages > 1;
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, hasPages);
	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, hasPages);

	// did the selected horizontal menu item position change?
	// if yes, update the instrument LED corresponding to that menu item position
	// store the last selected horizontal menu item position so that we don't update the LED's more than we have to
	if (const auto posOnPage = paging.selectedItemPositionOnPage; posOnPage != lastSelectedItemPosition) {
		updateSelectedMenuItemLED(posOnPage);
		lastSelectedItemPosition = posOnPage;
		currentKnobSpeed = 0.0f;
	}

	OLED::main.drawScreenTitle(getTitle(), false);
	renderPageCounters(paging);
	renderMenuItems(paging.visiblePageItems, *current_item_);

	OLED::markChanged();
}

void HorizontalMenu::renderPageCounters(const Paging& paging) {
	if (paging.totalPages <= 1) {
		return;
	}

	oled_canvas::Canvas& image = OLED::main;
	constexpr int32_t y = 1 + OLED_MAIN_TOPMOST_PIXEL;
	int32_t x = OLED_MAIN_WIDTH_PIXELS - kTextSpacingX - 1;

	// Draw total count
	DEF_STACK_STRING_BUF(currentPageNum, 2);
	currentPageNum.appendInt(paging.totalPages);
	image.drawString(currentPageNum.c_str(), x, y, kTextSpacingX, kTextSpacingY);
	x -= kTextSpacingX - 1;

	// Draw a separator line
	image.drawLine(x, y + kTextSpacingY - 2, x + 2, y + 1);
	x -= 6;

	// Draw the current one
	currentPageNum.clear();
	currentPageNum.appendInt(paging.visiblePageNumber + 1);
	image.drawString(currentPageNum.c_str(), x, y, kTextSpacingX, kTextSpacingY);
}

void HorizontalMenu::renderMenuItems(std::span<MenuItem*> items, const MenuItem* currentItem) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr int32_t baseY = 14 + OLED_MAIN_TOPMOST_PIXEL;
	constexpr int32_t columnWidth = OLED_MAIN_WIDTH_PIXELS / 4;
	int32_t currentX = 0;

	auto it = items.begin();
	for (size_t n = 0; n < items.size() && it != items.end(); n++) {
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
		if (isSelected && (items.size() > 1 || items[0]->getColumnSpan() < 4)) {
			switch (FlashStorage::accessibilityMenuHighlighting) {
			case MenuHighlighting::FULL_INVERSION: {
				// Highlight by inversion of the label or whole slot
				const bool highlightWholeSlot = !item->showColumnLabel() || item->isSubmenu();
				const int32_t startY = highlightWholeSlot ? baseY : labelY;
				const int32_t endY = highlightWholeSlot ? baseY + boxHeight - 1 : labelY + labelHeight - 1;
				image.invertAreaRounded(currentX + 1, boxWidth - 3, startY, endY);
				break;
			}
			case MenuHighlighting::PARTIAL_INVERSION:
				// Highlight by drawing outline
				image.drawRectangleRounded(currentX, baseY - 2, currentX + boxWidth - 2, baseY + boxHeight + 1,
				                           oled_canvas::BorderRadius::BIG);
				break;
			case MenuHighlighting::NO_INVERSION:
				// Highlight by drawing a line below the item
				image.invertArea(currentX, boxWidth - 1, baseY + boxHeight, OLED_MAIN_VISIBLE_HEIGHT + 1);
				break;
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

		child->selectEncoderAction(offset * calcNextKnobSpeed(offset));
		focusChild(child);
		displayNotification(child);

		// We don't want to return true for selectEncoderEditsInstrument(), since
		// that would trigger for scrolling in the menu as well.
		return soundEditor.markInstrumentAsEdited();
	}

	Submenu::selectEncoderAction(offset);
}

void HorizontalMenu::displayNotification(MenuItem* menuItem) {
	if (!menuItem->showNotification()) {
		return;
	}

	DEF_STACK_STRING_BUF(notificationValueBuf, kShortStringBufferSize);
	menuItem->getNotificationValue(notificationValueBuf);
	display->displayNotification(menuItem->getName(), notificationValueBuf.data());
}

void HorizontalMenu::switchVisiblePage(int32_t direction) {
	if (paging.totalPages <= 1) {
		return;
	}

	int32_t targetPageNumber = paging.visiblePageNumber + direction;

	// Wrap around
	if (targetPageNumber < 0) {
		targetPageNumber = paging.totalPages - 1;
	}
	else if (targetPageNumber >= paging.totalPages) {
		targetPageNumber = 0;
	}

	int32_t currentPageSpan = 0;
	int32_t currentPageNumber = 0;
	int32_t positionOnPage = 0;

	// Find the target item on the next / previous page
	for (const auto it : items) {
		if (layout == FIXED || isItemRelevant(it)) {
			const auto itemSpan = it->getColumnSpan();

			// Check if we need to move to the next page
			if (currentPageSpan + itemSpan > 4) {
				currentPageNumber++;
				currentPageSpan = 0;
				positionOnPage = 0;
			}

			// Select an item with the same position as on the previous selected page
			// If the item is not relevant, select the closest item instead
			if (currentPageNumber == targetPageNumber) {
				if (positionOnPage > paging.selectedItemPositionOnPage && isItemRelevant(*current_item_)) {
					break; // Past target position
				}
				current_item_ = std::ranges::find(items, it);
			}
			currentPageSpan += itemSpan;
			positionOnPage++;
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

void HorizontalMenu::selectMenuItem(std::span<MenuItem*> pageItems, const MenuItem* previous, int32_t selectedColumn) {
	// Find the item you're looking for by iterating through all items on the current page
	int32_t currentColumn = 0;

	for (size_t n = 0; n < pageItems.size(); n++) {
		MenuItem* item = pageItems[n];

		// Is this item covering the selected virtual column?
		if (selectedColumn >= currentColumn && selectedColumn < currentColumn + item->getColumnSpan()) {
			if (layout == FIXED && !isItemRelevant(item)) {
				// Item is disabled, do nothing
				return;
			}

			current_item_ = std::ranges::find(items, item);

			// is the item already selected?
			if (*current_item_ == previous) {
				return handleItemAction(*current_item_);
			}

			// Update the currently selected item
			updateDisplay();
			updatePadLights();
			(*current_item_)->updateAutomationViewParameter();
			return displayNotification(*current_item_);
		}

		currentColumn += item->getColumnSpan();
	}
}

HorizontalMenu::Paging& HorizontalMenu::preparePaging(std::span<MenuItem*> items, const MenuItem* currentItem) {
	static std::vector<MenuItem*> visiblePageItems;
	visiblePageItems.clear();
	visiblePageItems.reserve(4);

	uint8_t visiblePageNumber = 0;
	uint8_t currentPageSpan = 0;
	uint8_t totalPages = 1;
	uint8_t selectedItemPositionOnPage = 0;
	bool currentItemInThisPage = false;
	bool visiblePageCompleted = false;

	for (uint8_t i = 0; i < items.size(); ++i) {
		MenuItem* item = items[i];

		const bool isRelevant = isItemRelevant(item);
		if (isRelevant) {
			// Read value beforehand
			item->readCurrentValue();
		}
		// Skip non-relevant items in dynamic mode
		if (layout != FIXED && !isRelevant) {
			continue;
		}

		// If the item doesn't fit, start a new page
		const int32_t itemSpan = item->getColumnSpan();
		if (currentPageSpan + itemSpan > 4 && isRelevant) {
			if (currentItemInThisPage) {
				// Finalize visible page
				visiblePageCompleted = true;
				visiblePageNumber = totalPages - 1;
				currentItemInThisPage = false;
			}

			if (!visiblePageCompleted) {
				visiblePageItems.clear();
			}

			++totalPages;
			currentPageSpan = 0;
		}

		if (!visiblePageCompleted) {
			visiblePageItems.push_back(item);
		}

		if (item == currentItem) {
			selectedItemPositionOnPage = static_cast<uint8_t>(visiblePageItems.size() - 1);
			currentItemInThisPage = true;
		}

		currentPageSpan += itemSpan;
	}

	// Handle the last page
	if (currentItemInThisPage) {
		visiblePageNumber = totalPages - 1;
	}
	// Check if the page is single and has no items to render
	if (totalPages == 1 && currentPageSpan == 0) {
		totalPages = 0;
	}

	paging = Paging{visiblePageNumber, visiblePageItems, selectedItemPositionOnPage, totalPages};
	return paging;
}

/// When updating the selected horizontal menu item, you need to refresh the lit instrument LED's
void HorizontalMenu::updateSelectedMenuItemLED(int32_t itemNumber) const {
	const auto& pageItems = paging.visiblePageItems;
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

	if (display->hasPopupOfType(PopupType::NOTIFICATION)) {
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

	// Draw centered label
	const int32_t labelStartX = slotStartX + (slotWidth - labelWidth) / 2;
	image.drawString(label.c_str(), labelStartX, labelY, kTextSpacingX, kTextSpacingY);

	if (menuItem->getColumnSpan() > 1 && !menuItem->isSubmenu() && !isSelected) {
		// Draw small lines on the left and right side if the slot is too wide
		const int32_t y = labelY + 4;
		image.drawHorizontalLine(y, slotStartX + 4, labelStartX - 4);
		image.drawHorizontalLine(y, labelStartX + labelWidth + 2, slotStartX + slotWidth - 6);
	}
}

double HorizontalMenu::calcNextKnobSpeed(int8_t offset) {
	// - inertia and acceleration control how fast the knob speeds up in horizontal menus
	// - speedScale controls how we go from "raw speed" to speed used as offset multiplier
	// - min and max speed clamp the max effective speed
	//
	// current values have been tuned to be slow enough to feel easy to control, but fast
	// enough to go from 0 to 50 with one fast turn of the encoder. speedScale and min/max
	// could potentially be user-configurable in a small range.
	constexpr double acceleration = 0.1;
	constexpr double inertia = 1.0 - acceleration;
	constexpr double speedScale = 0.15;
	constexpr double minSpeed = 1.0;
	constexpr double maxSpeed = 5.0;
	constexpr double resetSpeedTimeThreshold = 0.3;

	// lastOffset and lastEncoderTime keep track of our direction and time
	static int8_t lastOffset = 0;
	static double lastEncoderTime = 0.0;
	const double time = getSystemTime();

	if (time - lastEncoderTime >= resetSpeedTimeThreshold || offset != lastOffset) {
		// too much time passed, or the knob direction changed, reset the speed
		currentKnobSpeed = 0.0;
	}
	else {
		// moving in the same direction, update speed
		currentKnobSpeed = currentKnobSpeed * inertia + 1.0 / (time - lastEncoderTime) * acceleration;
	}
	lastEncoderTime = time;
	lastOffset = offset;
	return std::clamp((currentKnobSpeed * speedScale), minSpeed, maxSpeed);
}

void HorizontalMenu::handleItemAction(MenuItem* menuItem) {
	if (!menuItem->isSubmenu() && !menuItem->allowToBeginSessionFromHorizontalMenu()) {
		return displayNotification(menuItem);
	}

	const auto result = menuItem->checkPermissionToBeginSession(
	    soundEditor.currentModControllable, soundEditor.currentSourceIndex, &soundEditor.currentMultiRange);

	if (result == MenuPermission::MUST_SELECT_RANGE) {
		soundEditor.currentMultiRange = nullptr;
		multiRangeMenu.menuItemHeadingTo = menuItem;
		menuItem = &multiRangeMenu;
	}

	soundEditor.enterSubmenu(menuItem);
}

} // namespace deluge::gui::menu_item
