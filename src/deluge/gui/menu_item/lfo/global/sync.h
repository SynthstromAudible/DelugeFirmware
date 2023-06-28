#pragma once
#include "definitions.h"
#include "gui/menu_item/sync_level.h"
#include "model/song/song.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"


namespace menu_item::lfo::global {

class Sync final : public SyncLevel {
public:
  using SyncLevel::SyncLevel;

	void readCurrentValue() {
		soundEditor.currentValue = syncTypeAndLevelToMenuOption(soundEditor.currentSound->lfoGlobalSyncType,
		                                                        soundEditor.currentSound->lfoGlobalSyncLevel);
	}
	void writeCurrentValue() {
		soundEditor.currentSound->setLFOGlobalSyncType(menuOptionToSyncType(soundEditor.currentValue));
		soundEditor.currentSound->setLFOGlobalSyncLevel(menuOptionToSyncLevel(soundEditor.currentValue));
		soundEditor.currentSound->setupPatchingForAllParamManagers(currentSong);
	}
};

} // namespace menu_item::lfo::global
