#pragma once

#include "definitions_cxx.hpp"
#include "gui/menu_item/menu_item.h"
#include "model/scale/preset_scales.h"
#include "model/song/song.h"
#include "storage/flash_storage.h"

#include <cstdint>

namespace deluge::gui::menu_item {

class ActiveScaleMenu : public MenuItem {
public:
	enum Kind { SONG, DEFAULT };
	ActiveScaleMenu(deluge::l10n::String newName, Kind kind_) : MenuItem(newName), currentPos(0), kind(kind_) {}
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) final override;
	void drawPixelsForOled() final override;
	void readValueAgain() final override;
	void selectEncoderAction(int32_t offset) final override;
	MenuItem* selectButtonPress() final override;
	void drawName() final override;

private:
	void drawSubmenuItemsForOled(std::span<uint8_t> scales, const uint8_t selected);
	bool isDisabled(uint8_t scaleIndex);
	void setDisabled(uint8_t scaleIndex, bool value);
	uint8_t currentPos;
	Kind kind;
};

} // namespace deluge::gui::menu_item
