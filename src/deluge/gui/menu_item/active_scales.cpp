#include "active_scales.h"

#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "hid/display/oled.h" //todo: this probably shouldn't be needed
#include "io/debug/log.h"
#include "model/scale/preset_scales.h"
#include "model/song/song.h"
#include "source_selection/range.h"
#include "source_selection/regular.h"
#include "util/functions.h"

#include <etl/vector.h>

namespace deluge::gui::menu_item {

void ActiveScaleMenu::beginSession(MenuItem* navigatedBackwardFrom) {
	readValueAgain();
}

void ActiveScaleMenu::readValueAgain() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		const char* name = getScaleName(static_cast<Scale>(currentPos));
		uint8_t dotPos = isDisabled(currentPos) ? 255 : 3;
		display->setScrollingText(name, 0, 600, -1, dotPos);
	}
}

void ActiveScaleMenu::drawName() {
	display->setScrollingText(getName().data());
}

void ActiveScaleMenu::drawPixelsForOled() {
	uint8_t sel;

	// Build a vector with visible scale items. No wrap-around.
	etl::vector<uint8_t, kOLEDMenuNumOptionsVisible> visible = {};
	if (currentPos == 0) {
		sel = 0;
		// beginning of the list
		for (uint8_t n = 0; n < kOLEDMenuNumOptionsVisible; n++) {
			uint8_t p = currentPos + n;
			visible.push_back(p);
		}
	}
	else if (currentPos == LAST_PRESET_SCALE) {
		sel = 2;
		// end of the list
		for (uint8_t n = 0; n < kOLEDMenuNumOptionsVisible; n++) {
			uint8_t p = currentPos + 1 - kOLEDMenuNumOptionsVisible + n;
			visible.push_back(p);
		}
	}
	else {
		sel = 1;
		// middle of the list
		for (uint8_t n = 0; n < kOLEDMenuNumOptionsVisible; n++) {
			uint8_t p = currentPos - 1 + n;
			visible.push_back(p);
		}
	}

	drawSubmenuItemsForOled(visible, sel);
}

// adapted from submenu.cpp & toggle.cpp
void ActiveScaleMenu::drawSubmenuItemsForOled(std::span<uint8_t> scales, const uint8_t selected) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	int32_t baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	for (int32_t o = 0; o < OLED_HEIGHT_CHARS - 1 && o < scales.size(); o++) {
		auto scale = scales[o];
		int32_t yPixel = o * kTextSpacingY + baseY;

		int32_t endX = OLED_MAIN_WIDTH_PIXELS - getSubmenuItemTypeRenderLength();

		const char* name = scalelikeNames[scale];

		// draw menu item string
		// if we're rendering type the menu item string will be cut off (so it doesn't overlap)
		// it will scroll below whenever you select that menu item
		image.drawString(name, kTextSpacingX, yPixel, kTextSpacingX, kTextSpacingY, 0, endX);

		// draw the toggle after the menu item string
		int32_t startX = getSubmenuItemTypeRenderIconStart();

		if (isDisabled(scale)) {
			image.drawGraphicMultiLine(deluge::hid::display::OLED::uncheckedBoxIcon, startX, yPixel, 7);
		}
		else {
			image.drawGraphicMultiLine(deluge::hid::display::OLED::checkedBoxIcon, startX, yPixel, 7);
		}

		// if you've selected a menu item, invert the area to show that it is selected
		// and setup scrolling in case that menu item is too long to display fully
		if (o == selected) {
			image.invertLeftEdgeForMenuHighlighting(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8);
			deluge::hid::display::OLED::setupSideScroller(0, name, kTextSpacingX, endX, yPixel, yPixel + 8,
			                                              kTextSpacingX, kTextSpacingY, true);
		}
	}
}

void ActiveScaleMenu::selectEncoderAction(int32_t offset) {
	// clamp instead of mod, because we want to avoid wraparound: otherwise when the list gets
	// long it's hard to tell when you've gone through, esp. since it's not in alphabetical order.
	currentPos = std::clamp((int8_t)(currentPos + offset), (int8_t)0, (int8_t)LAST_PRESET_SCALE);
	readValueAgain();
}

MenuItem* ActiveScaleMenu::selectButtonPress() {
	setDisabled(currentPos, !isDisabled(currentPos));
	readValueAgain();
	return NO_NAVIGATION;
}

bool ActiveScaleMenu::isDisabled(uint8_t scaleIndex) {
	if (kind == DEFAULT) {
		return FlashStorage::defaultDisabledPresetScales[scaleIndex];
	}
	else if (currentSong) {
		return currentSong->disabledPresetScales[scaleIndex];
	}
	else {
		return false;
	}
}

void ActiveScaleMenu::setDisabled(uint8_t scaleIndex, bool value) {
	if (kind == DEFAULT) {
		FlashStorage::defaultDisabledPresetScales[scaleIndex] = value;
	}
	else if (currentSong) {
		currentSong->disabledPresetScales[scaleIndex] = value;
	}
}

} // namespace deluge::gui::menu_item
