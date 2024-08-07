#include "horizontal_menu.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h" //todo: this probably shouldn't be needed
#include "util/container/static_vector.hpp"

#include "io/debug/log.h" // TODO: remove

namespace deluge::gui::menu_item {

void HorizontalMenu::focusChild(const MenuItem* child) {
	D_PRINTLN("focusChild(%s) for %s", child ? child->getName().data() : "nullptr", getName().data());
	// Update list of revelent items, and discover location of the child
	// item in it.
	relevantItems.clear();
	currentPos = 0; // first relevant item is an inoffensive default if we don't find focus
	for (size_t i = 0; i < items.size(); i++) {
		MenuItem* m = items[i];
		if (m && m->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
			relevantItems.push_back(m);
			D_PRINTLN(" + %s", m->getName().data());
			if (m == child) {
				currentPos = relevantItems.size() - 1;
				D_PRINTLN("  --> %s (%d)", child->getName().data(), currentPos);
			}
		}
		else {
			D_PRINTLN(" - %s", m ? m->getName().data() : "nullptr");
		}
	}
	if (relevantItems.size() == 0) {
		D_PRINTLN(" no relevant items!");
		currentPos = -1;
	}
	else if (child && relevantItems[currentPos] != child) {
		// This can happen with sufficiently magical menus, like unison. The _shortcut_ item for number
		// is different from the actual menu item - but since the number is first in the menu this works
		// right by accident for that special case. If we get even more special cases, then something
		// needs to be done.
		D_PRINTLN(" nenu %s focus item %s not relevant!", this->getName().data(), child->getName().data());
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
	else if (currentPos < 0) {
		return; // no relevant items
	}
	else if (Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
		relevantItems[currentPos]->readValueAgain();
	}
	else {
		relevantItems[currentPos]->drawName();
	}
}

void HorizontalMenu::drawPixelsForOled() {
	if (currentPos < 0) {
		return; // no relevant items
	}
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	int32_t baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	int32_t pageSize = std::min(relevantItems.size(), (size_t)4);
	int32_t pageCount = std::ceil(relevantItems.size() / (float)pageSize);
	int32_t page = currentPos / pageSize;
	int32_t pos = mod(currentPos, pageSize);
	int32_t first = page * pageSize;
	int32_t n = std::min(first + pageSize, (int32_t)relevantItems.size()) - first;

	int32_t boxHeight = OLED_MAIN_VISIBLE_HEIGHT - baseY;
	int32_t boxWidth = OLED_MAIN_WIDTH_PIXELS / pageSize;

	D_PRINTLN("pageSize=%d", pageSize);
	D_PRINTLN("pageCount=%d", pageCount);
	D_PRINTLN("page=%d", page);
	D_PRINTLN("pos=%d -> %d -> %s", pos, first + pos, relevantItems[first + pos]->getName().data());
	D_PRINTLN("n=%d", n);
	D_PRINTLN("first=%d, -> %s", first, relevantItems[first]->getName().data());
	D_PRINTLN("last=%d, -> %s", first + n - 1, relevantItems[first + n - 1]->getName().data());
	D_PRINTLN("curentPos=%d, -> %s", currentPos, relevantItems[currentPos]->getName().data());
	D_PRINTLN("curentPos=%d, -> %s", currentPos, relevantItems[currentPos]->getName().data());

	for (uint8_t i = 0; i < n; i++) {
		int32_t startX = boxWidth * i;
		// Update value!
		relevantItems[first + i]->readCurrentValue();
		relevantItems[first + i]->renderInHorizontalMenu(startX + 1, boxWidth, baseY, boxHeight);
	}

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
			if (p - 1 == page) {
				image.invertArea(endX, w + 1, pageY, pageY + kTextSpacingY);
			}
		}
	}
	image.invertArea(boxWidth * pos, boxWidth, baseY, baseY + boxHeight);
}

void HorizontalMenu::selectEncoderAction(int32_t offset) {
	if (currentPos < 0) {
		return; // no relevant items
	}
	MenuItem* child = relevantItems[currentPos];
	if (Buttons::isButtonPressed(deluge::hid::button::SHIFT) && !child->isSubmenu()) {
		child->setupNumberEditor();
		child->selectEncoderAction(offset);
		focusChild(child);
		// We don't want to return true for selectEncoderEditsInstrument(), since
		// that would trigger for scrolling in the menu as well.
		soundEditor.markInstrumentAsEdited();
	}
	else {
		currentPos = mod(currentPos + offset, relevantItems.size());
		updateDisplay();
	}
}

MenuItem* HorizontalMenu::selectButtonPress() {
	if (currentPos < 0) {
		return NO_NAVIGATION;
	}
	else {
		return relevantItems[currentPos];
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
