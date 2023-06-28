#pragma once
#include "storage/flash_storage.h"
#include "gui/menu_item/sync_level/relative_to_song.h"

namespace menu_item::record {
class Margins final : public Selection {
public:
  using Selection::Selection;
	void readCurrentValue() { soundEditor.currentValue = FlashStorage::audioClipRecordMargins; }
	void writeCurrentValue() { FlashStorage::audioClipRecordMargins = soundEditor.currentValue; }
};
}
