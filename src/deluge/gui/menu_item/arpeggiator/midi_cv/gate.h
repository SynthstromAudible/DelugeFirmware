#pragma once
#include "model/clip/instrument_clip.h"
#include "gui/menu_item/integer.h"
#include "model/song/song.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::arpeggiator::midi_cv {
class Gate final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() {
		InstrumentClip* current_clip = static_cast<InstrumentClip*>(currentSong->currentClip);
		int64_t arp_gate = (int64_t)current_clip->arpeggiatorGate + 2147483648;
		soundEditor.currentValue = (arp_gate * 50 + 2147483648) >> 32;
	}
	void writeCurrentValue() {
		((InstrumentClip*)currentSong->currentClip)->arpeggiatorGate =
		    (uint32_t)soundEditor.currentValue * 85899345 - 2147483648;
	}
	int getMaxValue() const { return 50; }
	bool isRelevant(Sound* sound, int whichThing) { return soundEditor.editingCVOrMIDIClip(); }
};
} // namespace menu_item::arpeggiator::midi_cv
