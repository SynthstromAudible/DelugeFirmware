#pragma once
#include "storage/flash_storage.h"
#include "gui/menu_item/bend_range.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::defaults {
class BendRange final : public menu_item::BendRange {
public:
  using menu_item::BendRange::BendRange;
	void readCurrentValue() { soundEditor.currentValue = FlashStorage::defaultBendRange[BEND_RANGE_MAIN]; }
	void writeCurrentValue() { FlashStorage::defaultBendRange[BEND_RANGE_MAIN] = soundEditor.currentValue; }
};
}
