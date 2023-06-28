#pragma once
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::arpeggiator {
class Octaves final : public Integer {
public:
	Octaves(char const* newName = NULL) : Integer(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentArpSettings->numOctaves; }
	void writeCurrentValue() { soundEditor.currentArpSettings->numOctaves = soundEditor.currentValue; }
	int getMinValue() const { return 1; }
	int getMaxValue() const { return 8; }
};
}
