#include "gui/menu_item/filter_route.h"

#include <cstdint>

namespace deluge::gui::menu_item {

void FilterRoutingMenu::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	const char* graphic;
	switch (getValue<FilterRoute>()) {
	case FilterRoute::HIGH_TO_LOW:
		graphic = "<=";
		break;
	case FilterRoute::LOW_TO_HIGH:
		graphic = "=>";
		break;
	default:
		graphic = "||";
	}
	int32_t pxLen = image.getStringWidthInPixels(graphic, kTextTitleSizeY);

	// Padding to center the string. If we can't center exactly, 1px right is better than 1px left.
	int32_t pad = (width - pxLen) / 2;
	if (pad * 2 < (width - pxLen)) {
		pad += 1;
	}
	image.drawString(graphic, startX + pad, startY + kTextSpacingY, kTextTitleSpacingX, kTextTitleSizeY, 0,
	                 startX + width);
}

} // namespace deluge::gui::menu_item
