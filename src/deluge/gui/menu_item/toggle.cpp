#include "toggle.h"
#include "hid/display/numeric_driver.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include <algorithm>

namespace deluge::gui::menu_item {

void Toggle::beginSession(MenuItem* navigatedBackwardFrom) {
	Value::beginSession(navigatedBackwardFrom);
#if HAVE_OLED
	soundEditor.menuCurrentScroll = 0;
#else
	drawValue();
#endif
}

void Toggle::selectEncoderAction(int offset) {
	const bool flip = offset & 0b1;
	if (flip) {
		this->value_ = !value_;
	}
	Value::selectEncoderAction(offset);
}

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
	soundEditor.menuCurrentScroll = std::clamp<int>(soundEditor.menuCurrentScroll, 0, 1);

	char const* options[] = {"Off", "On"};
	int selectedOption = this->value_ - soundEditor.menuCurrentScroll;

	int baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	for (int o = 0; o < 2; o++) {
		int yPixel = o * kTextSpacingY + baseY;

		OLED::drawString(options[o], kTextSpacingX, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                 kTextSpacingX, kTextSpacingY);

		if (o == selectedOption) {
			OLED::invertArea(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8, &OLED::oledMainImage[0]);
			OLED::setupSideScroller(0, options[o], kTextSpacingX, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8,
			                        kTextSpacingX, kTextSpacingY, true);
		}
	}
}
#endif
} // namespace deluge::gui::menu_item
