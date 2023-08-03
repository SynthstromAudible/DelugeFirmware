#include "toggle.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include "hid/display/oled.h"
#include <algorithm>

namespace deluge::gui::menu_item {

void Toggle::beginSession(MenuItem* navigatedBackwardFrom) {
	Value::beginSession(navigatedBackwardFrom);
	if (display.type == DisplayType::OLED) {
		soundEditor.menuCurrentScroll = 0;
	}
	else {
		drawValue();
	}
}

void Toggle::selectEncoderAction(int32_t offset) {
	const bool flip = offset & 0b1;
	if (flip) {
		this->value_ = !value_;
	}
	Value::selectEncoderAction(offset);
}

void Toggle::drawValue() {
	if (display.type == DisplayType::OLED) {
		renderUIsForOled();
	}
	else {
		display.setText(this->value_ ? "ON" : "OFF");
	}
}

void Toggle::drawPixelsForOled() {
	const int32_t val = static_cast<int32_t>(this->value_);
	// Move scroll
	soundEditor.menuCurrentScroll = std::clamp<int32_t>(soundEditor.menuCurrentScroll, 0, 1);

	char const* options[] = {"Off", "On"};
	int32_t selectedOption = this->value_ - soundEditor.menuCurrentScroll;

	int32_t baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	for (int32_t o = 0; o < 2; o++) {
		int32_t yPixel = o * kTextSpacingY + baseY;

		OLED::drawString(options[o], kTextSpacingX, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                 kTextSpacingX, kTextSpacingY);

		if (o == selectedOption) {
			OLED::invertArea(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8, &OLED::oledMainImage[0]);
			OLED::setupSideScroller(0, options[o], kTextSpacingX, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8,
			                        kTextSpacingX, kTextSpacingY, true);
		}
	}
}
} // namespace deluge::gui::menu_item
