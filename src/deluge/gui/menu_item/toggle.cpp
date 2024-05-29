#include "toggle.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include <algorithm>

namespace deluge::gui::menu_item {

void Toggle::beginSession(MenuItem* navigatedBackwardFrom) {
	Value::beginSession(navigatedBackwardFrom);
	if (display->haveOLED()) {
		soundEditor.menuCurrentScroll = 0;
	}
	else {
		drawValue();
	}
}

void Toggle::selectEncoderAction(int32_t offset) {
	const bool flip = offset & 0b1;
	if (flip) {
		this->setValue(!this->getValue());
	}
	Value::selectEncoderAction(offset);
}

void Toggle::drawValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		display->setText(this->getValue() //<
		                     ? l10n::get(l10n::String::STRING_FOR_ENABLED)
		                     : l10n::get(l10n::String::STRING_FOR_DISABLED));
	}
}

void Toggle::drawPixelsForOled() {
	deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;

	const int32_t val = static_cast<int32_t>(this->getValue());
	// Move scroll
	soundEditor.menuCurrentScroll = std::clamp<int32_t>(soundEditor.menuCurrentScroll, 0, 1);

	char const* options[] = {
	    l10n::get(l10n::String::STRING_FOR_DISABLED),
	    l10n::get(l10n::String::STRING_FOR_ENABLED),
	};
	int32_t selectedOption = this->getValue() - soundEditor.menuCurrentScroll;

	int32_t baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	for (int32_t o = 0; o < 2; o++) {
		int32_t yPixel = o * kTextSpacingY + baseY;

		canvas.drawString(options[o], kTextSpacingX, yPixel, kTextSpacingX, kTextSpacingY);

		if (o == selectedOption) {
			canvas.invertArea(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8);
			deluge::hid::display::OLED::setupSideScroller(0, options[o], kTextSpacingX, OLED_MAIN_WIDTH_PIXELS, yPixel,
			                                              yPixel + 8, kTextSpacingX, kTextSpacingY, true);
		}
	}
}
} // namespace deluge::gui::menu_item
