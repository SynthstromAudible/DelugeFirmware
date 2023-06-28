#pragma once
#include <cstring>
#include <cmath>

#include "gui/menu_item/integer.h"

#include "gui/ui/sound_editor.h"
#include "util/cfunctions.h"
#include "hid/display/numeric_driver.h"

#include "processing/engines/audio_engine.h"

namespace menu_item::reverb {
class Pan final : public Integer {
public:
	using Integer::Integer;
	void drawValue() {
		char buffer[5];
		intToString(std::abs(soundEditor.currentValue), buffer, 1);
		if (soundEditor.currentValue < 0) {
			strcat(buffer, "L");
		}
		else if (soundEditor.currentValue > 0) {
			strcat(buffer, "R");
		}
		numericDriver.setText(buffer, true);
	}

	void writeCurrentValue() { AudioEngine::reverbPan = ((int32_t)soundEditor.currentValue * 33554432); }

	void readCurrentValue() { soundEditor.currentValue = ((int64_t)AudioEngine::reverbPan * 128 + 2147483648) >> 32; }
	int getMaxValue() const { return 32; }
	int getMinValue() const { return -32; }
};
} // namespace menu_item::reverb
