#pragma once
#include "gui/menu_item/selection.h"
#include "io/midi/midi_engine.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::midi {
class Thru final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() { soundEditor.currentValue = midiEngine.midiThru; }
	void writeCurrentValue() { midiEngine.midiThru = soundEditor.currentValue; }
};
} // namespace menu_item::midi
