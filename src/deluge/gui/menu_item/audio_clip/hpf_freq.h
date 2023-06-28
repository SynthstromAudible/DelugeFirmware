#pragma once
#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::audio_clip {
class HPFFreq final : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;
#if !HAVE_OLED
	void drawValue() {
		if (soundEditor.currentValue == 0) {
			numericDriver.setText("OFF");
		}
		else UnpatchedParam::drawValue();
	}
#endif
};
} // namespace menu_item::audio_clip
