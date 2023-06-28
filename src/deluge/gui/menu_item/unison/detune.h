#pragma once
#include "model/model_stack.h"
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace menu_item::unison {
class Detune final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->unisonDetune; }
	void writeCurrentValue() {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();

		soundEditor.currentSound->setUnisonDetune(soundEditor.currentValue, modelStack);
	}
	int getMaxValue() const { return MAX_UNISON_DETUNE; }
};
} // namespace menu_item::unison
