#pragma once
#include "gui/menu_item/integer.h"
#include "dsp/reverb/freeverb/revmodel.hpp"
#include "gui/ui/sound_editor.h"
#include "processing/engines/audio_engine.h"
#include <cmath>

namespace menu_item::reverb {
class Dampening final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() { soundEditor.currentValue = std::round(AudioEngine::reverb.getdamp() * 50); }
	void writeCurrentValue() { AudioEngine::reverb.setdamp((float)soundEditor.currentValue / 50); }
	int getMaxValue() const { return 50; }
};
} // namespace menu_item::reverb
