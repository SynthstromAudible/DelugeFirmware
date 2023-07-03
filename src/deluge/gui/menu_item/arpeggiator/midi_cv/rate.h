#pragma once
#include "model/clip/instrument_clip.h"
#include "gui/menu_item/integer.h"
#include "model/song/song.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::arpeggiator::midi_cv {
class Rate final : public Integer {
public:
	Rate(char const* newName = NULL) : Integer(newName) {}
	void readCurrentValue() {
		soundEditor.currentValue =
		    (((int64_t)((InstrumentClip*)currentSong->currentClip)->arpeggiatorRate + 2147483648) * 50 + 2147483648)
		    >> 32;
	}
	void writeCurrentValue() {
		if (soundEditor.currentValue == 25) {
			((InstrumentClip*)currentSong->currentClip)->arpeggiatorRate = 0;
		}
		else {
			((InstrumentClip*)currentSong->currentClip)->arpeggiatorRate =
			    (uint32_t)soundEditor.currentValue * 85899345 - 2147483648;
		}
	}
	int getMaxValue() const { return 50; }
	bool isRelevant(Sound* sound, int whichThing) { return soundEditor.editingCVOrMIDIClip(); }
};
} // namespace menu_item::arpeggiator::midi_cv
