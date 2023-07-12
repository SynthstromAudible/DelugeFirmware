#include "toggle.h"
#include "hid/display/numeric_driver.h"
#include "gui/ui/sound_editor.h"

namespace deluge::gui::menu_item {

void Toggle::drawValue() {
#if HAVE_OLED
	renderUIsForOled();
#else
	numericDriver.setText(this->value_ ? "ON" : "OFF");
#endif
}

#if HAVE_OLED
void Toggle::drawPixelsForOled() {
	const int val = static_cast<int>(this->value_);
	// Move scroll
	if (soundEditor.menuCurrentScroll > val) {
		soundEditor.menuCurrentScroll = val;
	}
	else if (soundEditor.menuCurrentScroll < val - OLED_MENU_NUM_OPTIONS_VISIBLE + 1) {
		soundEditor.menuCurrentScroll = val - OLED_MENU_NUM_OPTIONS_VISIBLE + 1;
	}

	char const* options[] = {"Off", "On"};
	int selectedOption = this->value_ - soundEditor.menuCurrentScroll;

	drawItemsForOled(options, selectedOption);
}
#endif
} // namespace deluge::gui::menu_item
