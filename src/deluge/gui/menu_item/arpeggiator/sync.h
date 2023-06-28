#pragma once
#include "gui/menu_item/sync_level.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::arpeggiator {
class Sync final : public SyncLevel {
public:
	Sync(char const* newName = NULL) : SyncLevel(newName) {}
	void readCurrentValue() {
		soundEditor.currentValue = syncTypeAndLevelToMenuOption(soundEditor.currentArpSettings->syncType,
		                                                        soundEditor.currentArpSettings->syncLevel);
	}
	void writeCurrentValue() {
		soundEditor.currentArpSettings->syncType = menuOptionToSyncType(soundEditor.currentValue);
		soundEditor.currentArpSettings->syncLevel = menuOptionToSyncLevel(soundEditor.currentValue);
	}
};

} // namespace menu_item::arpeggiator
