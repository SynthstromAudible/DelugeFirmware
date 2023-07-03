#pragma once
#include "gui/menu_item/midi/preset.h"

namespace menu_item::midi {
class Bank final : public Preset {
public:
	using Preset::Preset;
	void readCurrentValue() { soundEditor.currentValue = ((InstrumentClip*)currentSong->currentClip)->midiBank; }
	void writeCurrentValue() {
		((InstrumentClip*)currentSong->currentClip)->midiBank = soundEditor.currentValue;
		if (((InstrumentClip*)currentSong->currentClip)->isActiveOnOutput()) {
			((InstrumentClip*)currentSong->currentClip)->sendMIDIPGM();
		}
	}
};
} // namespace menu_item::midi
