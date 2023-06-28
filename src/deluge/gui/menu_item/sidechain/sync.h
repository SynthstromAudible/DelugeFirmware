#pragma once
#include "gui/menu_item/sync_level.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"
#include "processing/engines/audio_engine.h"

namespace menu_item::sidechain {
class Sync final : public SyncLevel {
public:
	using SyncLevel::SyncLevel;

	int getNumOptions() override { return 10; };
	void readCurrentValue() override {
		soundEditor.currentValue = syncTypeAndLevelToMenuOption(soundEditor.currentCompressor->syncType,
		                                                        soundEditor.currentCompressor->syncLevel);
	}
	void writeCurrentValue() override {
		soundEditor.currentCompressor->syncType = menuOptionToSyncType(soundEditor.currentValue);
		soundEditor.currentCompressor->syncLevel = menuOptionToSyncLevel(soundEditor.currentValue);
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	bool isRelevant(Sound* sound, int whichThing) override {
		return !(soundEditor.editingReverbCompressor() && AudioEngine::reverbCompressorVolume < 0);
	}
};
}
