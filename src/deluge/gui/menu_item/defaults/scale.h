#pragma once
#include "storage/flash_storage.h"
#include "util/lookuptables/lookuptables.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::defaults {
class Scale final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() { soundEditor.currentValue = FlashStorage::defaultScale; }
	void writeCurrentValue() { FlashStorage::defaultScale = soundEditor.currentValue; }
	int getNumOptions() { return NUM_PRESET_SCALES + 2; }
	char const** getOptions() { return presetScaleNames; }
};
} // namespace menu_item::defaults
