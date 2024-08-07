#include "selection.h"
#include "gui/menu_item/menu_item.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"

#include "io/debug/log.h"

namespace deluge::gui::menu_item {
void Selection::drawValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	if (display->have7SEG()) {
		const auto options = this->getOptions();
		display->setScrollingText(options[this->getValue()].data());
	}
}

void Selection::drawPixelsForOled() {
	auto value = this->getValue();
	// Move scroll
	if (soundEditor.menuCurrentScroll > value) {
		soundEditor.menuCurrentScroll = value;
	}
	else if (soundEditor.menuCurrentScroll < value - kOLEDMenuNumOptionsVisible + 1) {
		soundEditor.menuCurrentScroll = value - kOLEDMenuNumOptionsVisible + 1;
	}

	const int32_t selectedOption = value - soundEditor.menuCurrentScroll;

	MenuItem::drawItemsForOled(std::span{getOptions().data(), getOptions().size()}, selectedOption,
	                           soundEditor.menuCurrentScroll);
}

// renders check box on OLED and dot on 7seg
void Selection::displayToggleValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawName();
	}
}

// handles rendering of the community features menu items that are identified as toggles
void Selection::renderSubmenuItemTypeForOled(int32_t yPixel) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	int32_t startX = getSubmenuItemTypeRenderIconStart();

	if (isToggle()) {
		if (getToggleValue()) {
			image.drawGraphicMultiLine(deluge::hid::display::OLED::checkedBoxIcon, startX, yPixel, 7);
		}
		else {
			image.drawGraphicMultiLine(deluge::hid::display::OLED::uncheckedBoxIcon, startX, yPixel, 7);
		}
	}
	else {
		image.drawGraphicMultiLine(deluge::hid::display::OLED::submenuArrowIcon, startX, yPixel, 7);
	}
}

void Selection::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	// Render item name

	std::string_view name = getName();
	size_t nameLen = std::min((size_t)(width / kTextSpacingX), name.size());
	// If we can fit the whole name, we do, if we can't we chop one letter off. It just looks and
	// feels better, at least with the names we have now.
	if (name.size() > nameLen) {
		nameLen -= 1;
	}
	DEF_STACK_STRING_BUF(shortName, 10);
	for (uint8_t p = 0; p < nameLen; p++) {
		shortName.append(name[p]);
	}
	image.drawString(shortName.c_str(), startX, startY, kTextSpacingX, kTextSpacingY, 0, startX + width);

	// Render current value

	std::string_view opt = getShortOptions()[this->getValue()];
	// Grab 6-char prefix with spaces removed.
	DEF_STACK_STRING_BUF(shortOpt, 6);
	for (uint8_t p = 0; p < opt.size() && shortOpt.size() < shortOpt.capacity(); p++) {
		if (opt[p] != ' ') {
			shortOpt.append(opt[p]);
		}
	}
	int32_t pxLen;
	// Trim characters from the end until it fits.
	while ((pxLen = image.getStringWidthInPixels(shortOpt.c_str(), kTextTitleSizeY)) >= width) {
		shortOpt.data()[shortOpt.size() - 1] = 0;
	}
	// Padding to center the string. If we can't center exactly, 1px right is better than 1px left.
	int32_t pad = (width + 1 - pxLen) / 2;
	image.drawString(shortOpt.c_str(), startX + pad, startY + kTextSpacingY + 2, kTextTitleSpacingX, kTextTitleSizeY, 0,
	                 startX + width);
}

} // namespace deluge::gui::menu_item
