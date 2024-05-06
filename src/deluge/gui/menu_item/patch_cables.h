#include "definitions_cxx.hpp"
#include "gui/menu_item/menu_item.h"
#include "util/containers.h"

#pragma once

namespace deluge::gui::menu_item {
class PatchCables : public MenuItem {
public:
	using MenuItem::MenuItem;
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) final;
	void selectEncoderAction(int32_t offset) final;
	void readValueAgain() final;
	MenuItem* selectButtonPress() final;
	uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) final;

	void drawPixelsForOled() final;
	int scrollPos = 0; // Each instance needs to store this separately
	void drawValue();

	void renderOptions();
	void blinkShortcuts();
	void blinkShortcutsSoon();
	ActionResult timerCallback() override;

	int32_t savedVal = 0;
	int32_t currentValue = 0;

	deluge::vector<std::string_view> options;

	PatchSource blinkSrc = PatchSource::NOT_AVAILABLE;
	PatchSource blinkSrc2 = PatchSource::NOT_AVAILABLE;
};

} // namespace deluge::gui::menu_item
