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
	this->setValue(offset > 0);
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
			canvas.invertLeftEdgeForMenuHighlighting(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8);
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
	using namespace deluge::hid::display;
	oled_canvas::Canvas& image = OLED::main;

	const int32_t startX = getSubmenuItemTypeRenderIconStart();
	image.drawGraphicMultiLine(getToggleValue() ? OLED::checkedBoxIcon : OLED::uncheckedBoxIcon, startX, yPixel,
	                           kSubmenuIconSpacingX);
}

void Toggle::renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) {
	using namespace deluge::hid::display;
	const Icon& icon = getValue() ? OLED::switcherIconOn : OLED::switcherIconOff;
	OLED::main.drawIconCentered(icon, slot.start_x, slot.width, slot.start_y - 1);
}

} // namespace deluge::gui::menu_item
