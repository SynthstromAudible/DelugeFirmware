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

	auto paging = calculateHorizontalMenuPaging();

	// did the selected horizontal menu item position change?
	// if yes, update the instrument LED corresponding to that menu item position
	// store the last selected horizontal menu item position so that we don't update the LED's more than we have to
	auto posOnPage = paging.currentItemPositionOnPage;
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

	auto it = paging.currentPageItems.begin();

	// Render the page
	for (size_t n = 0; it != paging.currentPageItems.end(); n++) {
		MenuItem* item = *it;
		int32_t boxWidth = (totalWidth * item->getColumnSpan()) / paging.currentPageSpan;

		if (currentX + boxWidth > totalWidth) {
			// Overflow occured: the item doesn't fit in the current page
			FREEZE_WITH_ERROR("DHOR");
		}

		if (n == posOnPage) {
			selectedStartX = currentX;
			selectedWidth = boxWidth;
		}

		if (horizontalMenuLayout == Layout::FIXED && !isItemRelevant(item)) {
			// In the fixed layout we just "disable" unrelevant item by drawing a dash as value
			item->renderColumnLabel(currentX + 1, boxWidth, baseY);

			const char disabledItemValueDash = '-';
			int32_t pxLen = image.getCharWidthInPixels(disabledItemValueDash, kTextTitleSizeY);
			int32_t pad = (boxWidth + 1 - pxLen) / 2;
			image.drawChar(disabledItemValueDash, currentX + pad, baseY + kTextSpacingY + 2, kTextTitleSpacingX,
			               kTextTitleSizeY, 0, currentX + boxWidth);
		}
		else {
			item->readCurrentValue();
			item->renderInHorizontalMenu(currentX + 1, boxWidth, baseY, boxHeight);
		}

		currentX += boxWidth;
		it = std::next(it);
	}

	// Render the page counters
	if (paging.pagesCount > 1) {
		int32_t extraY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 0 : 1;
		int32_t pageY = extraY + OLED_MAIN_TOPMOST_PIXEL;
		int32_t endX = OLED_MAIN_WIDTH_PIXELS;

		for (int32_t p = paging.pagesCount; p > 0; p--) {
			DEF_STACK_STRING_BUF(pageNum, 2);
			pageNum.appendInt(p);
			int32_t w = image.getStringWidthInPixels(pageNum.c_str(), kTextSpacingY);
			image.drawString(pageNum.c_str(), endX - w, pageY, kTextSpacingX, kTextSpacingY);
			endX -= w + 1;
			if (p - 1 == paging.currentPageNumber) {
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
	bool horizontal = renderingStyle() == RenderingStyle::HORIZONTAL;
	bool selectButtonPressed = Buttons::selectButtonPressUsedUp = Buttons::isButtonPressed(hid::button::SELECT_ENC);

	/* turning select encoder while any of these conditions are true can change horizontal menu value
	    i) Shift Button is stuck; or
	    ii) Audition pad pressed; or
	    iii) Horizontal menu alternative select encoder behaviour is enabled
	*/
	bool horizontalMenuValueChangeModifierEnabled = Buttons::isShiftStuck() || isUIModeActive(UI_MODE_AUDITIONING)
	                                                || FlashStorage::defaultAlternativeSelectEncoderBehaviour;

	// change horizontal menu value when either:
	// A) You're not pressing select encoder AND value change modifier is true
	// B) You're pressing select encoder AND value change modifier is disabled
	bool changeHorizontalMenuValue = (!selectButtonPressed && horizontalMenuValueChangeModifierEnabled)
	                                 || (selectButtonPressed && !horizontalMenuValueChangeModifierEnabled);

	MenuItem* child = *current_item_;

	if (horizontal && !child->isSubmenu() && changeHorizontalMenuValue) {
		child->selectEncoderAction(offset);
		focusChild(child);
		// We don't want to return true for selectEncoderEditsInstrument(), since
		// that would trigger for scrolling in the menu as well.
		return soundEditor.markInstrumentAsEdited();
	}
	if (horizontal) {
		// Undo any acceleration: we only want it for the items, not the menu itself.
		// We only do this for horizontal menus to allow fast scrolling with shift in vertical menus.
		offset = std::clamp(offset, (int32_t)-1, (int32_t)1);
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

	// in horizontal menu, use SYNTH / KIT / MIDI / CV buttons to select menu item
	// in currently displayed horizontal menu page
	if (b == SYNTH) {
		return selectHorizontalMenuItemOnVisiblePage(0);
	}
	else if (b == KIT) {
		return selectHorizontalMenuItemOnVisiblePage(1);
	}
	else if (b == MIDI) {
		return selectHorizontalMenuItemOnVisiblePage(2);
	}
	else if (b == CV) {
		return selectHorizontalMenuItemOnVisiblePage(3);
	}

	// forward other button presses to be handled by parent
	return Submenu::buttonAction(b, on, inCardRoutine);
}

/// Select a specific menu item on the currently displayed horizontal menu page
ActionResult HorizontalMenu::selectHorizontalMenuItemOnVisiblePage(int32_t itemNumber) {
	auto paging = calculateHorizontalMenuPaging();
	auto it = paging.currentPageItems.begin();

	// Find item you're looking for by iterating through all items on the current page
	for (size_t n = 0; n < paging.currentPageItems.size() && it != paging.currentPageItems.end(); n++) {
		// is this the item we're looking for?
		if (n == itemNumber) {
			if (horizontalMenuLayout == Layout::FIXED && !isItemRelevant(*it)) {
				// item is disabled, we can't select it so do nothing
				break;
			}

			// update currently selected item
			current_item_ = it;
			// re-render display
			updateDisplay();
			// update grid shortcuts for currently selected menu item
			updatePadLights();
			// update automation view editor parameter selection if it is currently open
			(*current_item_)->updateAutomationViewParameter();
			break;
		}
		// if we haven't found item we're looking for, check the next item.
		it = std::next(it);
	}
	return ActionResult::DEALT_WITH;
}

HorizontalMenuPaging HorizontalMenu::calculateHorizontalMenuPaging() {
	deluge::vector<MenuItem*> currentPageItems;
	int32_t currentPageNumber = 0;
	int32_t currentPageSpan = 0;
	int32_t currentItemPositionOnPage = 0;
	int32_t pagesCount = 1;

	std::vector<MenuItem*> relevantItems;
	std::ranges::copy_if(items, std::back_inserter(relevantItems), [this](MenuItem* item) {
		return horizontalMenuLayout == HorizontalMenu::Layout::FIXED || isItemRelevant(item);
	});

	struct ItemOnPage {
		MenuItem* item;
		int32_t pageNumber;
	};

	int32_t totalSpan = 0;
	deluge::vector<ItemOnPage> itemsOnPage;

	// For each item calc which page it belongs to, and calc total page count
	for (auto item : relevantItems) {
		int32_t itemSpan = item->getColumnSpan();
		if (totalSpan + itemSpan > 4) {
			++pagesCount;
			totalSpan = 0;
		}
		itemsOnPage.push_back({item, pagesCount - 1});
		totalSpan += itemSpan;
	}

	// Find the page number of the selected item
	for (auto& itemOnPage : itemsOnPage) {
		if (itemOnPage.item == *current_item_) {
			currentPageNumber = itemOnPage.pageNumber;
			break;
		}
	}

	// Find current page items & total span
	for (auto& itemOnPage : itemsOnPage) {
		if (itemOnPage.pageNumber == currentPageNumber) {
			currentPageItems.push_back(itemOnPage.item);
			currentPageSpan += itemOnPage.item->getColumnSpan();
		}
	}

	// Find current item position on page
	for (int32_t i = 0; i < currentPageItems.size(); i++) {
		if (currentPageItems[i] == *current_item_) {
			currentItemPositionOnPage = i;
			break;
		}
	}
	return {currentPageItems, currentPageNumber, currentPageSpan, currentItemPositionOnPage, pagesCount};
}

/// When updating the selected horizontal menu item, you need to refresh the lit instrument LED's
void HorizontalMenu::updateSelectedHorizontalMenuItemLED(int32_t itemNumber) {
	switch (itemNumber) {
	case 0:
		indicator_leds::setLedState(IndicatorLED::SYNTH, true);
		indicator_leds::setLedState(IndicatorLED::KIT, false);
		indicator_leds::setLedState(IndicatorLED::MIDI, false);
		indicator_leds::setLedState(IndicatorLED::CV, false);
		break;
	case 1:
		indicator_leds::setLedState(IndicatorLED::SYNTH, false);
		indicator_leds::setLedState(IndicatorLED::KIT, true);
		indicator_leds::setLedState(IndicatorLED::MIDI, false);
		indicator_leds::setLedState(IndicatorLED::CV, false);
		break;
	case 2:
		indicator_leds::setLedState(IndicatorLED::SYNTH, false);
		indicator_leds::setLedState(IndicatorLED::KIT, false);
		indicator_leds::setLedState(IndicatorLED::MIDI, true);
		indicator_leds::setLedState(IndicatorLED::CV, false);
		break;
	case 3:
		indicator_leds::setLedState(IndicatorLED::SYNTH, false);
		indicator_leds::setLedState(IndicatorLED::KIT, false);
		indicator_leds::setLedState(IndicatorLED::MIDI, false);
		indicator_leds::setLedState(IndicatorLED::CV, true);
		break;
	default:
	    // fallthrough
	    ;
	}
}

/// when exiting a horizontal menu, turn off the LED's and reset selected horizontal menu item position
/// so that next time you open a horizontal menu it refreshes the LED for the selected horizontal menu item
void HorizontalMenu::endSession() {
	lastSelectedHorizontalMenuItemPosition = kNoSelection;
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);
	indicator_leds::setLedState(IndicatorLED::CV, false);
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
