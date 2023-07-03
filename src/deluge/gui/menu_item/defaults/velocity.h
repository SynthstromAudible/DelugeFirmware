#pragma once
#include "storage/flash_storage.h"
#include "model/song/song.h"
#include "gui/ui/sound_editor.h"
#include "gui/menu_item/integer.h"

namespace menu_item::defaults {
class Velocity final : public Integer {
public:
	using Integer::Integer;
	int getMinValue() const { return 1; }
	int getMaxValue() const { return 127; }
	void readCurrentValue() { soundEditor.currentValue = FlashStorage::defaultVelocity; }
	void writeCurrentValue() {
		FlashStorage::defaultVelocity = soundEditor.currentValue;
		currentSong->setDefaultVelocityForAllInstruments(FlashStorage::defaultVelocity);
	}
};
} // namespace menu_item::defaults
