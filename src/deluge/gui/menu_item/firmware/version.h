#pragma once
#include "gui/menu_item/selection.h"
#include "hid/display/numeric_driver.h"
#include "gui/ui/sound_editor.h"

extern char const* firmwareString;

namespace menu_item::firmware {
class Version final : public MenuItem {
public:
	using MenuItem::MenuItem;

#if HAVE_OLED
	void drawPixelsForOled() {
		OLED::drawStringCentredShrinkIfNecessary(firmwareString, 22, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, 18,
		                                         20);
	}
#else
	void beginSession(MenuItem* navigatedBackwardFrom) { drawValue(); }

	void drawValue() { numericDriver.setScrollingText(firmwareString); }
#endif
};
} // namespace menu_item::firmware
