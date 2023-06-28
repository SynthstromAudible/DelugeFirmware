#pragma once
#include "model/mod_controllable/mod_controllable_audio.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/sync_level.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::delay {
class Sync final : public SyncLevel {
public:
  using SyncLevel::SyncLevel;

	void readCurrentValue() {
		soundEditor.currentValue = syncTypeAndLevelToMenuOption(soundEditor.currentModControllable->delay.syncType,
		                                                        soundEditor.currentModControllable->delay.syncLevel);
	}
	void writeCurrentValue() {
		soundEditor.currentModControllable->delay.syncType = menuOptionToSyncType(soundEditor.currentValue);
		soundEditor.currentModControllable->delay.syncLevel = menuOptionToSyncLevel(soundEditor.currentValue);
	}
};
}
