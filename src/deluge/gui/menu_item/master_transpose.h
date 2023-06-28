#pragma once
#include "gui/menu_item/integer.h"
#include "gui/menu_item/menu_item_with_cc_learning.h"
#include "gui/menu_item/patched_param.h"
#include "menu_item_with_cc_learning.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"
#include "model/model_stack.h"

namespace menu_item {
class MasterTranspose final : public Integer, public PatchedParam {
public:
	using Integer::Integer;
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->transpose; }
	void writeCurrentValue() {
		soundEditor.currentSound->transpose = soundEditor.currentValue;
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();
		soundEditor.currentSound->recalculateAllVoicePhaseIncrements(modelStack);
	}
	MenuItem* selectButtonPress() { return PatchedParam::selectButtonPress(); }
	uint8_t shouldDrawDotOnName() { return PatchedParam::shouldDrawDotOnName(); }
	uint8_t getPatchedParamIndex() { return PARAM_LOCAL_PITCH_ADJUST; }
	uint8_t getP() { return PARAM_LOCAL_PITCH_ADJUST; }
	uint8_t shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour) {
		return PatchedParam::shouldBlinkPatchingSourceShortcut(s, colour);
	}
	MenuItem* patchingSourceShortcutPress(int s, bool previousPressStillActive = false) {
		return PatchedParam::patchingSourceShortcutPress(s, previousPressStillActive);
	}
#if !HAVE_OLED
	void drawValue() { PatchedParam::drawValue(); }
#endif

	void unlearnAction() { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};

	int getMinValue() const { return -96; }
	int getMaxValue() const { return 96; }
};
}
