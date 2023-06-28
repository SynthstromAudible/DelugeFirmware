#pragma once
#include "gui/menu_item/selection.h"
#include "playback/playback_handler.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::trigger::in {
class AutoStart final : public Selection {
public:
  using Selection::Selection;
	void readCurrentValue() { soundEditor.currentValue = playbackHandler.analogClockInputAutoStart; }
	void writeCurrentValue() { playbackHandler.analogClockInputAutoStart = soundEditor.currentValue; }
};
}
