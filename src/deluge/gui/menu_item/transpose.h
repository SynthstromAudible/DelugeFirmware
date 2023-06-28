#pragma once
#include "decimal.h"
#include "patched_param.h"

namespace menu_item {

class Transpose : public Decimal, public PatchedParam {
public:
	Transpose(char const* newName = NULL, int newP = 0) : PatchedParam(newP), Decimal(newName) {}
	MenuItem* selectButtonPress() final { return PatchedParam::selectButtonPress(); }
	virtual int getMinValue() const final { return -9600; }
	virtual int getMaxValue() const final { return 9600; }
	virtual int getNumDecimalPlaces() const final { return 2; }
	uint8_t getPatchedParamIndex() final { return PatchedParam::getPatchedParamIndex(); }
	uint8_t shouldDrawDotOnName() final { return PatchedParam::shouldDrawDotOnName(); }
	uint8_t shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour) final {
		return PatchedParam::shouldBlinkPatchingSourceShortcut(s, colour);
	}
	MenuItem* patchingSourceShortcutPress(int s, bool previousPressStillActive = false) final {
		return PatchedParam::patchingSourceShortcutPress(s, previousPressStillActive);
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(::MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) final {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};
};

} // namespace menu_item
