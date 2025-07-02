#include "enumeration.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"

namespace deluge::gui::menu_item {
void Enumeration::beginSession(MenuItem* navigatedBackwardFrom) {
	Value::beginSession(navigatedBackwardFrom);
	drawValue();
}

bool Enumeration::wrapAround() {
	// This is the legacy behaviour, but OLED should wrap at least in some contexts
	// as well probably.
	return display->have7SEG();
}

void Enumeration::selectEncoderAction(int32_t offset) {
	int32_t numOptions = size();
	int32_t startValue = getValue();

	int32_t nextValue = startValue + offset;
	// valid values are [0, numOptions), so on OLED and in shif + select, clamp to valid values
	if (wrapAround()) {
		nextValue = mod(nextValue, numOptions);
	}
	else {
		nextValue = std::clamp<int32_t>(nextValue, 0, numOptions - 1);
	}

	setValue(nextValue);

	// reset offset to account for wrapping
	offset = nextValue - startValue;
	Value::selectEncoderAction(offset);
}

void Enumeration::drawValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	if (display->have7SEG()) {
		display->setTextAsNumber(getValue());
	}
}

void Enumeration::getShortOption(StringBuf& opt) {
	opt.appendInt(getValue());
}

void Enumeration::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	// Render current value
	DEF_STACK_STRING_BUF(shortOpt, kShortStringBufferSize);
	getShortOption(shortOpt);

	image.drawStringCentered(shortOpt, startX, startY + 3, kTextSpacingX, kTextSpacingY, width);
}

} // namespace deluge::gui::menu_item
