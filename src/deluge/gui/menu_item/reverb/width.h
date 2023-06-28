#pragma once
#include "gui/menu_item/integer.h"
#include "dsp/reverb/freeverb/revmodel.hpp"
#include "gui/ui/sound_editor.h"
#include "processing/engines/audio_engine.h"
#include <cmath>

namespace menu_item::reverb {
class Width final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() { soundEditor.currentValue = std::round(AudioEngine::reverb.getwidth() * 50); }
	void writeCurrentValue() { AudioEngine::reverb.setwidth((float)soundEditor.currentValue / 50); }
	int getMaxValue() const { return 50; }
};
} // namespace menu_item::reverb
