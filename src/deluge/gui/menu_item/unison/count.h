#pragma once
#include "gui/menu_item/integer.h"
#include "model/model_stack.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace menu_item::unison {
class Count final : public Integer {
public:
	Count(char const* newName = NULL) : Integer(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->numUnison; }
	void writeCurrentValue() {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();
		soundEditor.currentSound->setNumUnison(soundEditor.currentValue, modelStack);
	}
	int getMinValue() const { return 1; }
	int getMaxValue() const { return maxNumUnison; }
};
} // namespace menu_item::unison
