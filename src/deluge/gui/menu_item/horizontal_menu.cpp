#include "horizontal_menu.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h" //todo: this probably shouldn't be needed
#include "util/container/static_vector.hpp"

#include "io/debug/log.h" // TODO: remove

namespace deluge::gui::menu_item {

bool isItemRelevant(MenuItem* item) {
	return item->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex);
}

void HorizontalMenu::focusChild(const MenuItem* child) {
	D_PRINTLN("focusChild(%s) for %s", child ? child->getName().data() : "nullptr", getName().data());
	// Set new current item.
	if (child) {
		currentItem = std::find(items.begin(), items.end(), child);
	}
	// If the item wasn't found or isn't relevant, set to first relevant one instead.
	if (currentItem == items.end() || !isItemRelevant(*currentItem)) {
		currentItem = std::find_if(items.begin(), items.end(), isItemRelevant);
	}
	// Log it.
	if (currentItem != items.end()) {
		D_PRINTLN(" - focus: %s", (*currentItem)->getName().data());
	} else {
		D_PRINTLN(" - no focus!");
	}
}

void HorizontalMenu::beginSession(MenuItem* navigatedBackwardFrom) {
	focusChild(navigatedBackwardFrom);
	updateDisplay();
}

void HorizontalMenu::updateDisplay() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else if (currentItem == items.end()) {
		return; // no relevant items
	}
	else if (Buttons::isShiftButtonPressed()) {
		(*currentItem)->readValueAgain();
	}
	else {
		(*currentItem)->drawName();
	}
}

void HorizontalMenu::drawPixelsForOled() {
	if (currentItem == items.end()) {
		return; // no relevant items
	}
	D_PRINTLN("HorizontalMenu::drawPixelsForOled()");

	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	int32_t baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	int32_t nTotal = std::count_if(items.begin(), items.end(), isItemRelevant);
	int32_t nBefore = std::count_if(items.begin(), currentItem, isItemRelevant);

	int32_t pageSize = std::min<int32_t>(nTotal, 4);
	int32_t pageCount = std::ceil(nTotal / (float)pageSize);
	int32_t currentPage = nBefore / pageSize;
	int32_t posOnPage = mod(nBefore, pageSize);
	int32_t pageStart = currentPage * pageSize;

	D_PRINTLN("  nTotal=%d", nTotal);
	D_PRINTLN("  nBefore=%d", nBefore);
	D_PRINTLN("  pageSize=%d", pageSize);
	D_PRINTLN("  pageCount=%d", pageCount);
	D_PRINTLN("  page=%d", currentPage);
	D_PRINTLN("  posOnPage=%d -> %d", posOnPage, pageStart + posOnPage);
	D_PRINTLN("  pageStart=%d", pageStart);

	// Scan to beginning of the visible page:
	auto it = items.begin();
	for (size_t n = 0; n < pageStart; n++) {
		it = std::next(std::find_if(it, items.end(), isItemRelevant));
		if (it != items.end()) {
			D_PRINTLN("  scan: %s", (*it)->getName().data());
		}
	}
	D_PRINTLN("  start: %s", (*it)->getName().data());

	int32_t boxHeight = OLED_MAIN_VISIBLE_HEIGHT - baseY;
	int32_t boxWidth = OLED_MAIN_WIDTH_PIXELS / pageSize;

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

void HorizontalMenu::selectEncoderAction(int32_t offset) {
	if (currentItem != items.end()) {
		return; // no relevant items
	}
	MenuItem* child = *currentItem;
	if (Buttons::isShiftButtonPressed() && !child->isSubmenu()) {
		child->setupNumberEditor();
		child->selectEncoderAction(offset);
		focusChild(child);
		// We don't want to return true for selectEncoderEditsInstrument(), since
		// that would trigger for scrolling in the menu as well.
		soundEditor.markInstrumentAsEdited();
	}
	else if (offset > 0) {
		auto next = std::find_if(currentItem, items.end(), isItemRelevant);
		if (next != items.end()) {
			currentItem = next;
		} else {
			currentItem = std::find_if(items.begin(), items.end(), isItemRelevant);
		}
	}
	else if (offset < 0) {
		auto prev = std::find_if(std::reverse_iterator(currentItem), items.rend(), isItemRelevant);
		if (prev != items.rend()) {
			currentItem = prev.base();
		} else {
			currentItem = std::find_if(items.rbegin(), items.rend(), isItemRelevant).base();
		}
	}
}

MenuItem* HorizontalMenu::selectButtonPress() {
	if (currentItem != items.end()) {
		return NO_NAVIGATION;
	}
	else {
		return *currentItem;
	}
}

bool HorizontalMenu::isRelevant(ModControllableAudio* modControllable, int32_t whichThing) {
	// It would be nice if we could rely that focusChild() has been called, but that seems a bit
	// optimistic, so we need to check all the items instead.
	for (size_t i = 0; i < items.size(); i++) {
		MenuItem* m = items[i];
		if (m->isRelevant(modControllable, whichThing)) {
			return true;
		}
	}
	return false;
}

} // namespace deluge::gui::menu_item
