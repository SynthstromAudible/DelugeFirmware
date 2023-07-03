#pragma once
#include "gui/menu_item/patched_param.h"

namespace menu_item::patched_param {
class Integer : public PatchedParam, public menu_item::IntegerContinuous {
public:
	Integer(char const* newName = NULL, int newP = 0) : PatchedParam(newP), IntegerContinuous(newName) {}
#if !HAVE_OLED
	void drawValue() {
		PatchedParam::drawValue();
	}
#endif
	ParamDescriptor getLearningThing() final {
		return PatchedParam::getLearningThing();
	}
	int getMaxValue() const {
		return PatchedParam::getMaxValue();
	}
	int getMinValue() const {
		return PatchedParam::getMinValue();
	}
	uint8_t shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour) final {
		return PatchedParam::shouldBlinkPatchingSourceShortcut(s, colour);
	}

	uint8_t shouldDrawDotOnName() final {
		return PatchedParam::shouldDrawDotOnName();
	}
	MenuItem* selectButtonPress() final {
		return PatchedParam::selectButtonPress();
	}

	uint8_t getPatchedParamIndex() final {
		return PatchedParam::getPatchedParamIndex();
	}
	MenuItem* patchingSourceShortcutPress(int s, bool previousPressStillActive = false) final {
		return PatchedParam::patchingSourceShortcutPress(s, previousPressStillActive);
	}

	void unlearnAction() final {
		MenuItemWithCCLearning::unlearnAction();
	}
	bool allowsLearnMode() final {
		return MenuItemWithCCLearning::allowsLearnMode();
	}
	void learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) final {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};

protected:
	void readCurrentValue();
	void writeCurrentValue() final;
	virtual int32_t getFinalValue();
};
} // namespace menu_item::patched_param
