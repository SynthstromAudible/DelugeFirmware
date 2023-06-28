#pragma once
#include "model/mod_controllable/mod_controllable_audio.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/sync_level.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::delay {

class PingPong final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentModControllable->delay.pingPong; }
	void writeCurrentValue() { soundEditor.currentModControllable->delay.pingPong = soundEditor.currentValue; }
};

} // namespace menu_item::delay
