#pragma once
#include "model/mod_controllable/mod_controllable_audio.h"
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::fx {

class Clipping final : public IntegerWithOff {
public:
	using IntegerWithOff::IntegerWithOff;

	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentModControllable->clippingAmount; }
	void writeCurrentValue() { soundEditor.currentModControllable->clippingAmount = soundEditor.currentValue; }
	int getMaxValue() const { return 15; }
};

} // namespace menu_item::fx
