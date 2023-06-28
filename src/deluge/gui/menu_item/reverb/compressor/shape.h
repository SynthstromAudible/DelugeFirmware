#pragma once
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"
#include "processing/engines/audio_engine.h"

namespace menu_item::reverb::compressor {

class Shape final : public Integer {
public:
	Shape(char const* newName = NULL) : Integer(newName) {}
	void readCurrentValue() {
		soundEditor.currentValue = (((int64_t)AudioEngine::reverbCompressorShape + 2147483648) * 50 + 2147483648) >> 32;
	}
	void writeCurrentValue() {
		AudioEngine::reverbCompressorShape = (uint32_t)soundEditor.currentValue * 85899345 - 2147483648;
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	int getMaxValue() const { return 50; }
	bool isRelevant(Sound* sound, int whichThing) { return (AudioEngine::reverbCompressorVolume >= 0); }
};
} // namespace menu_item::reverb::compressor
