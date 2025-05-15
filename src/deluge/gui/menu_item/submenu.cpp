#include "submenu.h"
#include "processing/sound/sound.h"

#include "etl/vector.h"
#include "gui/views/automation_view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "model/settings/runtime_feature_settings.h"
#include "storage/flash_storage.h"
#include <algorithm>

namespace deluge::gui::menu_item {
void Submenu::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.currentMultiRange = nullptr;

	if (thingIndex.has_value()) {
		const auto thingIndexValue = thingIndex.value();
		soundEditor.currentSourceIndex = thingIndexValue;
		soundEditor.currentSource = &soundEditor.currentSound->sources[thingIndexValue];
		soundEditor.currentSampleControls = &soundEditor.currentSource->sampleControls;
	}

	focusChild(navigatedBackwardFrom);
	if (display->have7SEG()) {
		updateDisplay();
	}
}

bool Submenu::focusChild(const MenuItem* child) {
	// Set new current item.
	auto prev = current_item_;
	if (child != nullptr) {
		current_item_ = std::find(items.begin(), items.end(), child);
	}
	// If the item wasn't found or isn't relevant, set to first relevant one instead.
	if (current_item_ == items.end() || !isItemRelevant(*current_item_)) {
		current_item_ = std::ranges::find_if(items, isItemRelevant); // Find first relevant item.
	}
	// Log it.
	if (current_item_ != items.end()) {
		return true;
	}
	else {
		return false;
	}
}

void Submenu::updateDisplay() {
	if (!focusChild(nullptr)) {
		// no relevant items, back out
		soundEditor.goUpOneLevel();
	}
	else if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		(*current_item_)->drawName();
	}
}

void Submenu::drawPixelsForOled() {
	// Collect items before the current item, this is possibly more than we need.
	etl::vector<MenuItem*, kOLEDMenuNumOptionsVisible> before = {};
	for (auto it = current_item_ - 1; it != items.begin() - 1 && before.size() < before.capacity(); it--) {
		MenuItem* menuItem = (*it);
		if (menuItem->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
			before.push_back(menuItem);
		}
	}
	std::reverse(before.begin(), before.end());

	// Collect current item and fill the tail
	etl::vector<MenuItem*, kOLEDMenuNumOptionsVisible> after = {};
	for (auto it = current_item_; it != items.end() && after.size() < after.capacity(); it++) {
		MenuItem* menuItem = (*it);
		if (menuItem->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
			after.push_back(menuItem);
		}
	}

	// Ideally we'd have the selected item in the middle (rounding down for even cases)
	// ...but sometimes that's not going to happen.
	size_t pos = (kOLEDMenuNumOptionsVisible - 1) / 2;
	size_t tail = kOLEDMenuNumOptionsVisible - pos;
	if (before.size() < pos) {
		pos = before.size();
		tail = std::min((int32_t)(kOLEDMenuNumOptionsVisible - pos), (int32_t)after.size());
	}
	else if (after.size() < tail) {
		tail = after.size();
		pos = std::min((int32_t)(kOLEDMenuNumOptionsVisible - tail), (int32_t)before.size());
	}

	// Put it together.
	etl::vector<MenuItem*, kOLEDMenuNumOptionsVisible> visible;
	visible.insert(visible.begin(), before.end() - pos, before.end());
	visible.insert(visible.begin() + pos, after.begin(), after.begin() + tail);

	drawSubmenuItemsForOled(visible, pos);
}

void HorizontalMenu::drawPixelsForOled() {
	if (renderingStyle() != RenderingStyle::HORIZONTAL) {
		Submenu::drawPixelsForOled();
		return;
	}

	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	paging = splitMenuItemsByPages();

	// Lit the scale and cross-screen buttons LEDs to indicate that they can be used to switch between pages
	const auto hasPages = paging.pages.size() > 1;
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, hasPages);
	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, hasPages);

	// did the selected horizontal menu item position change?
	// if yes, update the instrument LED corresponding to that menu item position
	// store the last selected horizontal menu item position so that we don't update the LED's more than we have to
	auto posOnPage = paging.selectedItemPositionOnPage;
	if (posOnPage != lastSelectedHorizontalMenuItemPosition) {
		lastSelectedHorizontalMenuItemPosition = posOnPage;
		updateSelectedHorizontalMenuItemLED(posOnPage);
	}

	int32_t baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;
	int32_t boxHeight = OLED_MAIN_VISIBLE_HEIGHT - baseY;
	int32_t totalWidth = OLED_MAIN_WIDTH_PIXELS;
	int32_t currentX = 0;
	int32_t selectedStartX = 0;
	int32_t selectedWidth = 0;

	auto& visiblePage = paging.getVisiblePage();
	auto it = visiblePage.items.begin();

	// Render the page
	for (size_t n = 0; n < visiblePage.items.size() && it != visiblePage.items.end(); n++) {
		MenuItem* item = *it;
		const int32_t boxWidthRelative = 4 / (item->getColumnSpan() * visiblePage.spanMultiplier);
		const int32_t boxWidth = totalWidth / boxWidthRelative;

		if (currentX + boxWidth > totalWidth) {
			// Overflow occured: the item doesn't fit in the current page
			FREEZE_WITH_ERROR("DHOR");
		}

		if (n == posOnPage) {
			selectedStartX = currentX;
			selectedWidth = boxWidth;
		}

		if (horizontalMenuLayout == Layout::FIXED && !isItemRelevant(item)) {
			// Draw a dash as value indicating that the item is disabled
			item->renderColumnLabel(currentX + 1, boxWidth, baseY);

			const char disabledItemValueDash = '-';
			int32_t pxLen = image.getCharWidthInPixels(disabledItemValueDash, kTextTitleSizeY);
			int32_t pad = ((boxWidth - pxLen) / 2) - 1;
			image.drawChar(disabledItemValueDash, currentX + pad, baseY + kTextSpacingY + 2, kTextTitleSpacingX,
			               kTextTitleSizeY, 0, currentX + boxWidth);
		}
		else {
			item->readCurrentValue();
			item->renderInHorizontalMenu(currentX + 1, boxWidth, baseY, boxHeight);
		}

		// Draw dotted separator at the end of the menu item
		// Only if this item is not the selected item or its immediate neighbors
		if (n != posOnPage - 1 && n != posOnPage && currentX + boxWidth != totalWidth) {
			int32_t lineX = currentX + boxWidth - 1;
			for (int32_t y = baseY; y < baseY + boxHeight + 2; y += 2) {
				image.drawPixel(lineX, y);
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
			if (p - 1 == visiblePage.number) {
				image.invertArea(endX, w + 1, pageY, pageY + kTextSpacingY);
			}
		}
	}

	// Highlight the selected item
	image.invertArea(selectedStartX, selectedWidth, baseY, baseY + boxHeight);
}

void Submenu::drawSubmenuItemsForOled(std::span<MenuItem*> options, const int32_t selectedOption) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	int32_t baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	for (int32_t o = 0; o < OLED_HEIGHT_CHARS - 1 && o < options.size(); o++) {
		auto* menuItem = options[o];
		int32_t yPixel = o * kTextSpacingY + baseY;

		int32_t endX = OLED_MAIN_WIDTH_PIXELS - menuItem->getSubmenuItemTypeRenderLength();

		// draw menu item string
		// if we're rendering type the menu item string will be cut off (so it doesn't overlap)
		// it will scroll below whenever you select that menu item
		image.drawString(menuItem->getName(), kTextSpacingX, yPixel, kTextSpacingX, kTextSpacingY, 0, endX);

		// draw the menu item type after the menu item string
		menuItem->renderSubmenuItemTypeForOled(yPixel);

		// if you've selected a menu item, invert the area to show that it is selected
		// and setup scrolling in case that menu item is too long to display fully
		if (o == selectedOption) {
			image.invertLeftEdgeForMenuHighlighting(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8);
			deluge::hid::display::OLED::setupSideScroller(0, menuItem->getName(), kTextSpacingX, endX, yPixel,
			                                              yPixel + 8, kTextSpacingX, kTextSpacingY, true);
		}
	}
}

bool Submenu::wrapAround() {
	return display->have7SEG() || renderingStyle() == RenderingStyle::HORIZONTAL;
}

void Submenu::selectEncoderAction(int32_t offset) {
	if (current_item_ == items.end()) {
		return;
	}

	MenuItem* child = *current_item_;

	bool horizontal = renderingStyle() == RenderingStyle::HORIZONTAL;
	if (horizontal) {
		bool selectButtonPressed = Buttons::selectButtonPressUsedUp = Buttons::isButtonPressed(hid::button::SELECT_ENC);
		if (!child->isSubmenu() && !selectButtonPressed) {
			child->selectEncoderAction(offset);
			focusChild(child);
			// We don't want to return true for selectEncoderEditsInstrument(), since
			// that would trigger for scrolling in the menu as well.
			soundEditor.markInstrumentAsEdited();
		}
		return;
	}

	if (offset > 0) {
		// Scan items forward, counting relevant items.
		auto lastRelevant = current_item_;
		do {
			current_item_++;
			if (current_item_ == items.end()) {
				if (wrapAround()) {
					current_item_ = items.begin();
				}
				else {
					current_item_ = lastRelevant;
					break;
				}
			}
			if ((*current_item_)->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
				lastRelevant = current_item_;
				offset--;
			}
		} while (offset > 0);
	}
	else if (offset < 0) {
		// Scan items backwad, counting relevant items.
		auto lastRelevant = current_item_;
		do {
			if (current_item_ == items.begin()) {
				if (wrapAround()) {
					current_item_ = items.end();
				}
				else {
					current_item_ = lastRelevant;
					break;
				}
			}
			current_item_--;
			if ((*current_item_)->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
				lastRelevant = current_item_;
				offset++;
			}
		} while (offset < 0);
	}
	updateDisplay();
	updatePadLights();
	(*current_item_)->updateAutomationViewParameter();
}

bool Submenu::shouldForwardButtons() {
	// Should we deliver buttons to selected menu item instead?
	return (*current_item_)->isSubmenu() == false && renderingStyle() == RenderingStyle::HORIZONTAL;
}

MenuItem* Submenu::selectButtonPress() {
	if (shouldForwardButtons()) {
		return (*current_item_)->selectButtonPress();
	}
	else {
		return *current_item_;
	}
}

ActionResult Submenu::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	if (shouldForwardButtons()) {
		return (*current_item_)->buttonAction(b, on, inCardRoutine);
	}
	else {
		return MenuItem::buttonAction(b, on, inCardRoutine);
	}
}

ActionResult HorizontalMenu::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	if (!on) {
		return Submenu::buttonAction(b, on, inCardRoutine);
	}

	// use SYNTH / KIT / MIDI / CV buttons to select menu item in currently displayed horizontal menu page
	// use SCALE / CROSS SCREEN buttons to switch between pages
	switch (b) {
	case SYNTH:
		return selectHorizontalMenuItemOnVisiblePage(0);
	case KIT:
		return selectHorizontalMenuItemOnVisiblePage(1);
	case MIDI:
		return selectHorizontalMenuItemOnVisiblePage(2);
	case CV:
		return selectHorizontalMenuItemOnVisiblePage(3);
	case CROSS_SCREEN_EDIT:
	case SCALE_MODE:
		return switchVisiblePage(b == CROSS_SCREEN_EDIT ? 1 : -1);
	}

	// forward other button presses to be handled by parent
	return Submenu::buttonAction(b, on, inCardRoutine);
}

ActionResult HorizontalMenu::switchVisiblePage(int32_t direction) {
	if (paging.pages.size() <= 1) {
		// No need to switch pages if there's only one page
		return ActionResult::DEALT_WITH;
	}

	int32_t targetPageNumber = paging.visiblePageNumber + direction;

	// Adjust targetPageNumber to cycle through pages
	if (targetPageNumber < 0) {
		targetPageNumber = paging.pages.size() - 1;
	}
	else if (targetPageNumber >= paging.pages.size()) {
		targetPageNumber = 0;
	}

	paging.visiblePageNumber = targetPageNumber;

	// update currently selected item
	current_item_ = std::find(items.begin(), items.end(), *paging.getVisiblePage().items.begin());
	updateDisplay();
	updatePadLights();
	updateSelectedHorizontalMenuItemLED(0);

	// Update automation view editor parameter selection if it is currently open
	(*current_item_)->updateAutomationViewParameter();

	return ActionResult::DEALT_WITH;
}

/// Selects the menu item covering the given virtual column on the visible page
ActionResult HorizontalMenu::selectHorizontalMenuItemOnVisiblePage(int32_t selectedColumn) {
	auto& visiblePage = paging.getVisiblePage();

	// Find item you're looking for by iterating through all items on the current page
	int32_t currentColumn = 0;
	for (size_t n = 0; n < visiblePage.items.size(); n++) {
		MenuItem* item = visiblePage.items[n];
		int32_t actualItemSpan = item->getColumnSpan() * visiblePage.spanMultiplier;

		// is this item covering the selected virtual column?
		if (selectedColumn >= currentColumn && selectedColumn < currentColumn + actualItemSpan) {
			if (horizontalMenuLayout == Layout::FIXED && !isItemRelevant(item)) {
				// item is disabled, do nothing
				break;
			}
			// update currently selected item
			current_item_ = std::find(items.begin(), items.end(), item);
			// re-render display
			updateDisplay();
			// update grid shortcuts for currently selected menu item
			updatePadLights();
			// update automation view editor parameter selection if it is currently open
			(*current_item_)->updateAutomationViewParameter();
			break;
		}
		currentColumn += actualItemSpan;
	}
	return ActionResult::DEALT_WITH;
}

HorizontalMenu::Paging HorizontalMenu::splitMenuItemsByPages() {
	std::vector<PageInfo> pages;
	std::vector<MenuItem*> currentPageItems;
	int32_t currentPageNumber = 0;
	int32_t currentPageSpan = 0;

	int32_t visiblePageNumber = 0;
	int32_t selectedItemPositionOnPage = 0;

	for (auto* item : items) {
		const auto renderItem = horizontalMenuLayout == HorizontalMenu::Layout::FIXED || isItemRelevant(item);
		if (!renderItem) {
			continue;
		}

		int32_t itemSpan = item->getColumnSpan();
		if (currentPageSpan + itemSpan > 4) {
			// Finalize the current page
			const auto spanMultiplier = currentPageSpan == 3 ? 1 : 4 / currentPageSpan;
			pages.push_back(PageInfo{currentPageNumber, spanMultiplier, currentPageItems});

			// Start a new page
			currentPageItems = {};
			currentPageSpan = 0;
			++currentPageNumber;
		}

		if (item == *current_item_) {
			visiblePageNumber = currentPageNumber;
			selectedItemPositionOnPage = currentPageItems.size();
		}

		currentPageItems.push_back(item);
		currentPageSpan += itemSpan;
	}

	if (!currentPageItems.empty()) {
		// Finalize the current page
		const auto spanMultiplier = currentPageSpan == 3 ? 1 : 4 / currentPageSpan;
		pages.push_back(PageInfo{currentPageNumber, spanMultiplier, currentPageItems});
	}

	return Paging{visiblePageNumber, selectedItemPositionOnPage, pages};
}

/// When updating the selected horizontal menu item, you need to refresh the lit instrument LED's
void HorizontalMenu::updateSelectedHorizontalMenuItemLED(int32_t itemNumber) {
	auto& visiblePage = paging.getVisiblePage();
	auto* selectedItem = visiblePage.items[itemNumber];

	int32_t startColumn = 0;
	int32_t endColumn = 0;
	for (auto* item : visiblePage.items) {
		const auto actualItemSpan = item->getColumnSpan() * visiblePage.spanMultiplier;
		if (item == selectedItem) {
			endColumn = startColumn + actualItemSpan;
			break;
		}
		startColumn += actualItemSpan;
	}

	// Light up all buttons whose columns are covered by the selected item
	std::vector<bool> ledStates{false, false, false, false};
	if (visiblePage.items.size() > 1) {
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
/// so that next time you open a horizontal menu it refreshes the LED for the selected horizontal menu item
void HorizontalMenu::endSession() {
	lastSelectedHorizontalMenuItemPosition = kNoSelection;
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
}

deluge::modulation::params::Kind Submenu::getParamKind() {
	if (shouldForwardButtons()) {
		return (*current_item_)->getParamKind();
	}
	else {
		return MenuItem::getParamKind();
	}
}

uint32_t Submenu::getParamIndex() {
	if (shouldForwardButtons()) {
		return (*current_item_)->getParamIndex();
	}
	else {
		return MenuItem::getParamIndex();
	}
}

void Submenu::unlearnAction() {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*current_item_)->unlearnAction();
	}
}

bool Submenu::allowsLearnMode() {
	if (soundEditor.getCurrentMenuItem() == this) {
		return (*current_item_)->allowsLearnMode();
	}
	return false;
}

void Submenu::learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*current_item_)->learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}
}
void Submenu::learnProgramChange(MIDICable& cable, int32_t channel, int32_t programNumber) {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*current_item_)->learnProgramChange(cable, channel, programNumber);
	}
}

bool Submenu::learnNoteOn(MIDICable& cable, int32_t channel, int32_t noteCode) {
	if (soundEditor.getCurrentMenuItem() == this) {
		return (*current_item_)->learnNoteOn(cable, channel, noteCode);
	}
	return false;
}

Submenu::RenderingStyle HorizontalMenu::renderingStyle() {
	if (display->haveOLED() && runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::HorizontalMenus)) {
		return RenderingStyle::HORIZONTAL;
	}
	return RenderingStyle::VERTICAL;
}

void Submenu::updatePadLights() {
	if (renderingStyle() == RenderingStyle::HORIZONTAL && current_item_ != items.end()) {
		soundEditor.updatePadLightsFor(*current_item_);
	}
	else {
		MenuItem::updatePadLights();
	}
}

bool Submenu::usesAffectEntire() {
	if (current_item_ != items.end()
	    && (renderingStyle() == RenderingStyle::HORIZONTAL || !(*current_item_)->shouldEnterSubmenu())) {
		// If the menu is Horizontal or is the focused menu item is a toggle,
		// then we should use affect-entire from this item
		return (*current_item_)->usesAffectEntire();
	}
	return false;
}

MenuItem* Submenu::patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive) {
	if (renderingStyle() == RenderingStyle::HORIZONTAL && current_item_ != items.end()) {
		return (*current_item_)->patchingSourceShortcutPress(s, previousPressStillActive);
	}
	else {
		return MenuItem::patchingSourceShortcutPress(s, previousPressStillActive);
	}
}

} // namespace deluge::gui::menu_item
