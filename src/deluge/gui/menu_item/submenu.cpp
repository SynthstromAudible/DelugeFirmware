#include "submenu.h"

#include "etl/vector.h"
#include "gui/views/automation_view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/settings/runtime_feature_settings.h"
#include "storage/flash_storage.h"
#include <algorithm>

namespace deluge::gui::menu_item {
void Submenu::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.currentMultiRange = nullptr;
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
	if (renderingStyle() == RenderingStyle::VERTICAL) {
		drawVerticalMenu();
	}
	else {
		drawHorizontalMenu();
	}
}

void Submenu::drawVerticalMenu() {
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

void Submenu::drawHorizontalMenu() {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	int32_t baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	int32_t nTotal = std::count_if(items.begin(), items.end(), isItemRelevant);
	int32_t nBefore = std::count_if(items.begin(), current_item_, isItemRelevant);

	int32_t pageSize = std::min<int32_t>(nTotal, 4);
	int32_t pageCount = std::ceil(nTotal / (float)pageSize);
	int32_t currentPage = nBefore / pageSize;
	int32_t posOnPage = mod(nBefore, pageSize);
	int32_t pageStart = currentPage * pageSize;

	// Scan to beginning of the visible page:
	auto it = std::find_if(items.begin(), items.end(), isItemRelevant);
	for (size_t n = 0; n < pageStart; n++) {
		it = std::find_if(std::next(it), items.end(), isItemRelevant);
	}

	int32_t boxHeight = OLED_MAIN_VISIBLE_HEIGHT - baseY;
	int32_t boxWidth = OLED_MAIN_WIDTH_PIXELS / std::min<int32_t>(nTotal - pageStart, 4);

	// Render the page
	for (size_t n = 0; n < pageSize && it != items.end(); n++) {
		MenuItem* item = *it;
		int32_t startX = boxWidth * n;
		item->readCurrentValue();
		item->renderInHorizontalMenu(startX + 1, boxWidth, baseY, boxHeight);
		// next relevant item.
		it = std::find_if(std::next(it), items.end(), isItemRelevant);
	}

	// Render the page counters
	if (pageCount > 1) {
		int32_t extraY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 0 : 1;
		int32_t pageY = extraY + OLED_MAIN_TOPMOST_PIXEL;

		int32_t endX = OLED_MAIN_WIDTH_PIXELS;

		for (int32_t p = pageCount; p > 0; p--) {
			DEF_STACK_STRING_BUF(pageNum, 2);
			pageNum.appendInt(p);
			int32_t w = image.getStringWidthInPixels(pageNum.c_str(), kTextSpacingY);
			image.drawString(pageNum.c_str(), endX - w, pageY, kTextSpacingX, kTextSpacingY);
			endX -= w + 1;
			if (p - 1 == currentPage) {
				image.invertArea(endX, w + 1, pageY, pageY + kTextSpacingY);
			}
		}
	}
	image.invertArea(boxWidth * posOnPage, boxWidth, baseY, baseY + boxHeight);
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

Submenu::RenderingStyle Submenu::renderingStyle() {
	if (display->haveOLED() && this->supportsHorizontalRendering()
	    && runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::HorizontalMenus)) {
		return RenderingStyle::HORIZONTAL;
	}
	else {
		return RenderingStyle::VERTICAL;
	}
}

void Submenu::updatePadLights() {
	if (renderingStyle() == RenderingStyle::HORIZONTAL && current_item_ != items.end()) {
		soundEditor.updatePadLightsFor(*current_item_);
	}
	else {
		MenuItem::updatePadLights();
	}
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
