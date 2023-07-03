#pragma once
#include "storage/flash_storage.h"
#include "gui/menu_item/selection.h"
#include "hid/display/numeric_driver.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"

namespace menu_item::defaults {
class Magnitude final : public Selection {
public:
	using Selection::Selection;
	int getNumOptions() { return 7; }
	void readCurrentValue() { soundEditor.currentValue = FlashStorage::defaultMagnitude; }
	void writeCurrentValue() { FlashStorage::defaultMagnitude = soundEditor.currentValue; }
#if HAVE_OLED
	void drawPixelsForOled() {
		char buffer[12];
		intToString(96 << soundEditor.currentValue, buffer);
		OLED::drawStringCentred(buffer, 20 + OLED_MAIN_TOPMOST_PIXEL, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                        18, 20);
	}
#else
	void drawValue() {
		numericDriver.setTextAsNumber(96 << soundEditor.currentValue);
	}
#endif
};
} // namespace menu_item::defaults
