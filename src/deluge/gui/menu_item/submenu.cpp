#include "submenu.h"
#include "hid/display/display.h"
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
	static_vector<std::string_view, kOLEDMenuNumOptionsVisible> nextItemNames = {};
	int32_t idx = selectedRow;
	for (auto it = current_item_; it != this->items.end() && idx < kOLEDMenuNumOptionsVisible; it++) {
		if ((*it)->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
			nextItemNames.push_back((*it)->getName());
			idx++;
		}
	}

	static_vector<std::string_view, kOLEDMenuNumOptionsVisible> prevItemNames = {};
	idx = selectedRow - 1;
	for (auto it = current_item_ - 1; it != this->items.begin() - 1 && idx >= 0; it--) {
		if ((*it)->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)) {
			prevItemNames.push_back((*it)->getName());
			idx--;
		}
	}
	std::reverse(prevItemNames.begin(), prevItemNames.end());

	if (!prevItemNames.empty()) {
		prevItemNames.insert(prevItemNames.end(), nextItemNames.begin(), nextItemNames.end());
		drawItemsForOled(prevItemNames, selectedRow);
	}
	else {
		drawItemsForOled(nextItemNames, selectedRow);
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
