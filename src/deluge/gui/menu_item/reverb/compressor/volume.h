#pragma once
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"
#include "processing/engines/audio_engine.h"

namespace menu_item::reverb::compressor {

class Volume final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() { soundEditor.currentValue = AudioEngine::reverbCompressorVolume / 21474836; }
	void writeCurrentValue() {
		AudioEngine::reverbCompressorVolume = soundEditor.currentValue * 21474836;
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	int getMaxValue() const { return 50; }
	int getMinValue() const { return -1; }
#if !HAVE_OLED
	void drawValue() {
		if (soundEditor.currentValue < 0) numericDriver.setText("AUTO");
		else Integer::drawValue();
	}
#endif
};

} // namespace menu_item::reverb::compressor
