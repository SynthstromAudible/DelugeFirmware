#include "selection.h"
#include "gui/menu_item/menu_item.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"

namespace deluge::gui::menu_item {
void Selection::drawValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	if (display->have7SEG()) {
		const auto options = this->getOptions();
		display->setText(options[this->getValue()].data());
	}
}

void Selection::drawPixelsForOled() {
	auto value = this->getValue();
	// Move scroll
	if (soundEditor.menuCurrentScroll > value) {
		soundEditor.menuCurrentScroll = value;
	}
	else if (soundEditor.menuCurrentScroll < this->getValue() - kOLEDMenuNumOptionsVisible + 1) {
		soundEditor.menuCurrentScroll = value - kOLEDMenuNumOptionsVisible + 1;
	}

	const int32_t selectedOption = value - soundEditor.menuCurrentScroll;

	MenuItem::drawItemsForOled(std::span{getOptions().data(), getOptions().size()}, selectedOption, soundEditor.menuCurrentScroll);
}
}
