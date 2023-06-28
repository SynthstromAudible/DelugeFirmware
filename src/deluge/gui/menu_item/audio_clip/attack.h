#pragma once
#include "model/clip/audio_clip.h"
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "model/song/song.h"

namespace menu_item::audio_clip {
class Attack final : public Integer {
public:
  using Integer::Integer;

	void readCurrentValue() {
		soundEditor.currentValue =
		    (((int64_t)((AudioClip*)currentSong->currentClip)->attack + 2147483648) * 50 + 2147483648) >> 32;
	}
	void writeCurrentValue() {
		((AudioClip*)currentSong->currentClip)->attack = (uint32_t)soundEditor.currentValue * 85899345 - 2147483648;
	}
	int getMaxValue() const { return 50; }
};
} // namespace menu_item::audio_clip
