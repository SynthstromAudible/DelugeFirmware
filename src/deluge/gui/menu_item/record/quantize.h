#pragma once
#include "storage/flash_storage.h"
#include "gui/menu_item/sync_level/relative_to_song.h"

namespace menu_item::record {
class Quantize final : public sync_level::RelativeToSong {
public:
	using RelativeToSong::RelativeToSong;
	void readCurrentValue() { soundEditor.currentValue = FlashStorage::recordQuantizeLevel; }
	void writeCurrentValue() { FlashStorage::recordQuantizeLevel = soundEditor.currentValue; }
};
} // namespace menu_item::record
