#pragma once
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"
#include "processing/engines/audio_engine.h"

namespace menu_item::compressor {
class Attack final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() {
		soundEditor.currentValue =
		    getLookupIndexFromValue(soundEditor.currentCompressor->attack >> 2, attackRateTable, 50);
	}
	void writeCurrentValue() {
		soundEditor.currentCompressor->attack = attackRateTable[soundEditor.currentValue] << 2;
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	int getMaxValue() const { return 50; }
	bool isRelevant(Sound* sound, int whichThing) {
		return !(soundEditor.editingReverbCompressor() && AudioEngine::reverbCompressorVolume < 0);
	}
};
} // namespace menu_item::compressor
