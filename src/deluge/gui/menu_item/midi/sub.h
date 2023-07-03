#pragma once
#include "gui/menu_item/midi/preset.h"

namespace menu_item::midi {
class Sub final : public Preset {
public:
	using Preset::Preset;
	void readCurrentValue() { soundEditor.currentValue = ((InstrumentClip*)currentSong->currentClip)->midiSub; }
	void writeCurrentValue() {
		((InstrumentClip*)currentSong->currentClip)->midiSub = soundEditor.currentValue;
		if (((InstrumentClip*)currentSong->currentClip)->isActiveOnOutput()) {
			((InstrumentClip*)currentSong->currentClip)->sendMIDIPGM();
		}
	}
};
} // namespace menu_item::midi
