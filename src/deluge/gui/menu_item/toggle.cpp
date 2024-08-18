#include "toggle.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include <algorithm>

namespace deluge::gui::menu_item {

void Toggle::beginSession(MenuItem* navigatedBackwardFrom) {
	Value::beginSession(navigatedBackwardFrom);
	drawValue();
}

void Toggle::selectEncoderAction(int32_t offset) {
	const bool flip = offset & 0b1;
	if (flip) {
		this->setValue(!this->getValue());
	}
	Value::selectEncoderAction(offset);
}

const char* Toggle::getNameFor(bool enabled) {
	return enabled ? l10n::get(l10n::String::STRING_FOR_ENABLED) : l10n::get(l10n::String::STRING_FOR_DISABLED);
}

void Toggle::drawValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		display->setText(getNameFor(getValue()));
	}
}

void Toggle::drawPixelsForOled() {
	deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;

	int32_t yPixel = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	yPixel += OLED_MAIN_TOPMOST_PIXEL;

	bool selectedOption = getValue();
	bool order[2] = {false, true};
	for (bool o : order) {
		const char* name = getNameFor(o);
		canvas.drawString(name, kTextSpacingX, yPixel, kTextSpacingX, kTextSpacingY);

		if (o == selectedOption) {
			canvas.invertArea(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8);
			deluge::hid::display::OLED::setupSideScroller(0, name, kTextSpacingX, OLED_MAIN_WIDTH_PIXELS, yPixel,
			                                              yPixel + 8, kTextSpacingX, kTextSpacingY, true);
		}

		yPixel += kTextSpacingY;
	}
}

// renders check box on OLED and dot on 7seg
void Toggle::displayToggleValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawName();
	}
}

void Toggle::renderSubmenuItemTypeForOled(int32_t yPixel) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	int32_t startX = getSubmenuItemTypeRenderIconStart();

	if (getToggleValue()) {
		image.drawGraphicMultiLine(deluge::hid::display::OLED::checkedBoxIcon, startX, yPixel, 7);
	}
	else {
		image.drawGraphicMultiLine(deluge::hid::display::OLED::uncheckedBoxIcon, startX, yPixel, 7);
	}
}

// TODO: This almot identical with the patched & unpatched integer versions. Share code?
void Toggle::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	std::string_view name = getName();
	size_t len = std::min((size_t)(width / kTextSpacingX), name.size());
	// If we can fit the whole name, we do, if we can't we chop one letter off. It just looks and
	// feels better, at least with the names we have now.
	if (name.size() > len) {
		len -= 1;
	}
	std::string_view shortName(name.data(), len);
	image.drawString(shortName, startX, startY, kTextSpacingX, kTextSpacingY, 0, startX + width);

	const char* value;
	if (getToggleValue()) {
		value = "ON";
	}
	else {
		value = "OFF";
	}
	int32_t pxLen = image.getStringWidthInPixels(value, kTextTitleSizeY);
	// If not exactly centered, prefer 1px to the right.
	int32_t pad = (width + 1 - pxLen) / 2;
	image.drawString(value, startX + pad, startY + kTextSpacingY + 2, kTextTitleSpacingX, kTextTitleSizeY, 0,
	                 startX + width);
}

} // namespace deluge::gui::menu_item
