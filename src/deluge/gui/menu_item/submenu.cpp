#include "submenu.h"
#include "hid/display/display.h"
#include "hid/display/oled.h" //todo: this probably shouldn't be needed
#include "util/container/static_vector.hpp"

namespace deluge::gui::menu_item {
void Submenu::beginSession(MenuItem* navigatedBackwardFrom) {
	current_item_ = items.begin();
	soundEditor.currentMultiRange = nullptr;
	if (navigatedBackwardFrom != nullptr) {
		for (; *current_item_ != navigatedBackwardFrom; current_item_++) {
			if (current_item_ == items.end()) { // If desired item not found
				current_item_ = items.begin();
				break;
			}
		}
	}
	auto start = current_item_;
	bool wrapped = false;
	// loop through non-null items until we find a relevant one
	while ((*current_item_ != nullptr)
	       && !(*current_item_)->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)
	       && !(wrapped && current_item_ == start)) {
		current_item_++;
		if (current_item_ == items.end()) { // Not sure we need this since we don't wrap submenu items?
			current_item_ = items.begin();
			wrapped = true;
		}
	}
	if (display->have7SEG()) {
		updateDisplay();
	}
}

void Submenu::updateDisplay() {
	if (ensureCurrentItemIsRelevant()) {
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			(*current_item_)->drawName();
		}
	}
}

// sometimes when you refresh a menu, a menu item may become not relevant anymore depending on the context
// for example, refreshing the custom note iterance menu when selecting a different note with a different divisor
// this function ensures that when the menu is refreshed that only a relevant menu item is selected
bool Submenu::ensureCurrentItemIsRelevant() {
	// check if current item is relevant scrolling in backwards direction
	for (auto it = current_item_; it >= items.begin(); it--) {
		MenuItem* menuItem = (*it);
		if (menuItem->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
			current_item_ = it;
			return true;
		}
	}

	// if we still didn't find a relevant one...
	// then maybe there is a relevant one in the forward direction...
	for (auto it = current_item_; it <= items.end(); it++) {
		MenuItem* menuItem = (*it);
		if (menuItem->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
			current_item_ = it;
			return true;
		}
	}

	// if we still didn't find one...something went really wrong...exit out of this menu
	soundEditor.goUpOneLevel();
	return false;
}

void Submenu::drawPixelsForOled() {
	// Collect items before the current item, this is possibly more than we need.
	static_vector<MenuItem*, kOLEDMenuNumOptionsVisible> before = {};
	for (auto it = current_item_ - 1; it != items.begin() - 1 && before.size() < before.capacity(); it--) {
		MenuItem* menuItem = (*it);
		if (menuItem->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
			before.push_back(menuItem);
		}
	}
	std::reverse(before.begin(), before.end());

	// Collect current item and fill the tail
	static_vector<MenuItem*, kOLEDMenuNumOptionsVisible> after = {};
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
			image.invertLeftEdgeForMenuHighlighting(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8);
			deluge::hid::display::OLED::setupSideScroller(0, menuItem->getName(), kTextSpacingX, endX, yPixel,
			                                              yPixel + 8, kTextSpacingX, kTextSpacingY, true);
		}
	}
}

bool Submenu::wrapAround() {
	return display->have7SEG();
}

void Submenu::selectEncoderAction(int32_t offset) {
	if (!items.size()) {
		return;
	}
	auto lastRelevant = current_item_;
	if (offset > 0) {
		// Scan items forward, counting relevant items.
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
}

MenuItem* Submenu::selectButtonPress() {
	return *current_item_;
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

void Submenu::learnKnob(MIDIDevice* fromDevice, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*current_item_)->learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	}
}
void Submenu::learnProgramChange(MIDIDevice* fromDevice, int32_t channel, int32_t programNumber) {
	if (soundEditor.getCurrentMenuItem() == this) {
		(*current_item_)->learnProgramChange(fromDevice, channel, programNumber);
	}
}

bool Submenu::learnNoteOn(MIDIDevice* fromDevice, int32_t channel, int32_t noteCode) {
	if (soundEditor.getCurrentMenuItem() == this) {
		return (*current_item_)->learnNoteOn(fromDevice, channel, noteCode);
	}
	return false;
}
} // namespace deluge::gui::menu_item
