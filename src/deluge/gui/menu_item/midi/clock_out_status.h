#pragma once
#include "gui/menu_item/selection.h"
#include "playback/playback_handler.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::midi {
class ClockOutStatus final : public Selection {
public:
  using Selection::Selection;
	void readCurrentValue() { soundEditor.currentValue = playbackHandler.midiOutClockEnabled; }
	void writeCurrentValue() { playbackHandler.setMidiOutClockMode(soundEditor.currentValue); }
};
}
