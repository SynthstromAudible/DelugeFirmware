#include "submenu.h"
#include "hid/display/display.h"
#include "hid/display/oled.h" //todo: this probably shouldn't be needed
#include "util/container/static_vector.hpp"

namespace deluge::gui::menu_item {
void Submenu::beginSession(MenuItem* navigatedBackwardFrom) {
	current_item_ = items.begin();
	soundEditor.menuCurrentScroll = 0;
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
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		(*current_item_)->drawName();
	}
}

void Submenu::drawPixelsForOled() {
	int32_t selectedRow = soundEditor.menuCurrentScroll;

	// This finds the next relevant submenu item
	static_vector<MenuItem*, kOLEDMenuNumOptionsVisible> nextMenuItem = {};
	int32_t idx = selectedRow;
	for (auto it = current_item_; it != this->items.end() && idx < kOLEDMenuNumOptionsVisible; it++) {
		MenuItem* menuItem = (*it);
		if (menuItem->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
			nextMenuItem.push_back(menuItem);
			idx++;
		}
	}

	// This finds the previous relevant submenu item
	static_vector<MenuItem*, kOLEDMenuNumOptionsVisible> prevMenuItem = {};
	idx = selectedRow - 1;
	for (auto it = current_item_ - 1; it != this->items.begin() - 1 && idx >= 0; it--) {
		MenuItem* menuItem = (*it);
		if (menuItem->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
			prevMenuItem.push_back(menuItem);
			idx--;
		}
	}
	std::reverse(prevMenuItem.begin(), prevMenuItem.end());

	if (!prevMenuItem.empty()) {
		prevMenuItem.insert(prevMenuItem.end(), nextMenuItem.begin(), nextMenuItem.end());
		drawSubmenuItemsForOled(prevMenuItem, selectedRow);
	}
	else {
		drawSubmenuItemsForOled(nextMenuItem, selectedRow);
	}
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
		menuItem->renderSubmenuItemTypeForOled(endX + kTextSpacingX, yPixel);

		// if you've selected a menu item, invert the area to show that it is selected
		// and setup scrolling in case that menu item is too long to display fully
		if (o == selectedOption) {
			image.invertArea(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8);
			deluge::hid::display::OLED::setupSideScroller(0, menuItem->getName(), kTextSpacingX, endX, yPixel,
			                                              yPixel + 8, kTextSpacingX, kTextSpacingY, true);
		}
	}
}

void Submenu::selectEncoderAction(int32_t offset) {
	int32_t sign = (offset > 0) ? 1 : ((offset < 0) ? -1 : 0);
	auto thisSubmenuItem = current_item_;

	for (size_t i = 0; i < std::abs(offset); ++i) {
		do {
			if (offset >= 0) {
				thisSubmenuItem++;
				if (thisSubmenuItem >= items.end()) {
					if (display->haveOLED()) {
						updateDisplay();
						return;
					}
					// 7SEG wraps
					thisSubmenuItem = items.begin();
				}
			}
			else {
				if (thisSubmenuItem <= items.begin()) {
					if (display->haveOLED()) {
						updateDisplay();
						return;
					}
					// 7SEG wraps
					thisSubmenuItem = (items.end() - 1);
				}
				else {
					thisSubmenuItem--;
				}
			}
		} while (!(*thisSubmenuItem)->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex));

		current_item_ = thisSubmenuItem;

		if (display->haveOLED()) {
			soundEditor.menuCurrentScroll += sign;
			if (soundEditor.menuCurrentScroll < 0) {
				soundEditor.menuCurrentScroll = 0;
			}
			else if (soundEditor.menuCurrentScroll > kOLEDMenuNumOptionsVisible - 1) {
				soundEditor.menuCurrentScroll = kOLEDMenuNumOptionsVisible - 1;
			}
		}
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
