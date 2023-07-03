#pragma once
#include "gui/menu_item/integer.h"
#include "dsp/reverb/freeverb/revmodel.hpp"
#include "gui/ui/sound_editor.h"
#include "processing/engines/audio_engine.h"
#include <cmath>

namespace menu_item::reverb {
class RoomSize final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() { soundEditor.currentValue = std::round(AudioEngine::reverb.getroomsize() * 50); }
	void writeCurrentValue() { AudioEngine::reverb.setroomsize((float)soundEditor.currentValue / 50); }
	int getMaxValue() const { return 50; }
};
} // namespace menu_item::reverb
