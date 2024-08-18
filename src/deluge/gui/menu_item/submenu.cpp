#include "submenu.h"
#include "hid/display/display.h"
#include "hid/display/oled.h" //todo: this probably shouldn't be needed
#include "util/container/static_vector.hpp"

#include "io/debug/log.h"

namespace deluge::gui::menu_item {
void Submenu::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.currentMultiRange = nullptr; // TODO: is this actually needed?
	focusChild(navigatedBackwardFrom);
	if (display->have7SEG()) {
		updateDisplay();
	}
}

void Submenu::focusChild(const MenuItem* child) {
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
	}
	else {
		D_PRINTLN(" - no focus!");
	}
}

void Submenu::updateDisplay() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else if (currentItem == items.end()) {
		// no relevant items
		return;
	}
	else if (Buttons::isShiftButtonPressed()) {
		// Display the value instead of name on 7-seg if shift is held.
		(*currentItem)->readValueAgain();
	}
	else {
		(*currentItem)->drawName();
	}
}

void Submenu::drawPixelsForOled() {
	if (currentItem == items.end()) {
		// no relevant items
		return;
	}

	switch (renderingStyle()) {
	case RenderingStyle::VERTICAL:
		return drawVerticalMenu();
	case RenderingStyle::HORIZONTAL:
		return drawHorizontalMenu();
	default:
		__unreachable();
	}
}

void Submenu::drawVerticalMenu() {
	D_PRINTLN("Submenu::drawHVerticalMenu()");

	// Collect items before the current item, this is possibly more than we need.
	static_vector<MenuItem*, kOLEDMenuNumOptionsVisible> before = {};
	for (auto it = currentItem - 1; it != items.begin() - 1 && before.size() < before.capacity(); it--) {
		MenuItem* menuItem = (*it);
		if (menuItem->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
			before.push_back(menuItem);
		}
	}
	std::reverse(before.begin(), before.end());

	// Collect current item and fill the tail
	static_vector<MenuItem*, kOLEDMenuNumOptionsVisible> after = {};
	for (auto it = currentItem; it != items.end() && after.size() < after.capacity(); it++) {
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
	static_vector<MenuItem*, kOLEDMenuNumOptionsVisible> visible;
	visible.insert(visible.begin(), before.end() - pos, before.end());
	visible.insert(visible.begin() + pos, after.begin(), after.begin() + tail);

	drawSubmenuItemsForOled(visible, pos);
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
			image.invertArea(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8);
			deluge::hid::display::OLED::setupSideScroller(0, menuItem->getName(), kTextSpacingX, endX, yPixel,
			                                              yPixel + 8, kTextSpacingX, kTextSpacingY, true);
		}
	}
}

void Submenu::drawHorizontalMenu() {
	D_PRINTLN("Submenu::drawHorizontalMenu()");

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

bool Submenu::wrapAround() {
	return display->have7SEG() || renderingStyle() == RenderingStyle::HORIZONTAL;
}

void Submenu::selectEncoderAction(int32_t offset) {
	D_PRINTLN("Submenu::selectEncoderAction(%d)", offset);
	if (currentItem == items.end()) {
		D_PRINTLN("- no relevant items!");
		return;
	}
	D_PRINTLN("- current: %s", (*currentItem)->getName().data());
	MenuItem* child = *currentItem;
	if (Buttons::isShiftButtonPressed() && !child->isSubmenu() && renderingStyle() == RenderingStyle::HORIZONTAL) {
		child->setupNumberEditor();
		child->selectEncoderAction(offset);
		focusChild(child);
		// We don't want to return true for selectEncoderEditsInstrument(), since
		// that would trigger for scrolling in the menu as well.
		soundEditor.markInstrumentAsEdited();
	}
	if (offset > 0) {
		// Scan items forward, counting relevant items.
		auto lastRelevant = currentItem;
		do {
			currentItem++;
			if (currentItem == items.end()) {
				if (wrapAround()) {
					currentItem = items.begin();
				}
				else {
					currentItem = lastRelevant;
					break;
				}
			}
			if ((*currentItem)->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
				lastRelevant = currentItem;
				offset--;
			}
		} while (offset > 0);
	}
	else if (offset < 0) {
		// Scan items backwad, counting relevant items.
		auto lastRelevant = currentItem;
		do {
			if (currentItem == items.begin()) {
				if (wrapAround()) {
					currentItem = items.end();
				}
				else {
					currentItem = lastRelevant;
					break;
				}
			}
			currentItem--;
			if ((*currentItem)->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
				lastRelevant = currentItem;
				offset++;
			}
		} while (offset < 0);
	}
	updateDisplay(); // TODO: is this needed?
}

MenuItem* Submenu::selectButtonPress() {
	if (currentItem == items.end()) {
		return NO_NAVIGATION;
	}
	else {
		return *currentItem;
	}
}

bool Submenu::isRelevant(ModControllableAudio* modControllable, int32_t whichThing) {
	// Submenu is relevant if any of its items is relevant.
	auto isRelevantHere = [modControllable, whichThing](MenuItem* item) {
		return item->isRelevant(modControllable, whichThing);
	};
	return std::any_of(items.begin(), items.end(), isRelevantHere);
}

void Submenu::unlearnAction() {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*currentItem)->unlearnAction();
	}
}

bool Submenu::allowsLearnMode() {
	if (soundEditor.getCurrentMenuItem() == this) {
		return (*currentItem)->allowsLearnMode();
	}
	return false;
}

void Submenu::learnKnob(MIDIDevice* fromDevice, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*currentItem)->learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	}
}
void Submenu::learnProgramChange(MIDIDevice* fromDevice, int32_t channel, int32_t programNumber) {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*currentItem)->learnProgramChange(fromDevice, channel, programNumber);
	}
}

bool Submenu::learnNoteOn(MIDIDevice* fromDevice, int32_t channel, int32_t noteCode) {
	if (soundEditor.getCurrentMenuItem() == this) {
		return (*currentItem)->learnNoteOn(fromDevice, channel, noteCode);
	}
	return false;
}

} // namespace deluge::gui::menu_item
