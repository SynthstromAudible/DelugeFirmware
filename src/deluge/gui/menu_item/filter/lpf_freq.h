#pragma once
#include "modulation/patch/patch_cable_set.h"
#include "gui/menu_item/patched_param/integer_non_fm.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::filter {
class LPFFreq final : public patched_param::IntegerNonFM {
public:
	LPFFreq(char const* newName = 0, int newP = 0) : patched_param::IntegerNonFM(newName, newP) {}
#if !HAVE_OLED
	void drawValue() {
		if (soundEditor.currentValue == 50
		    && !soundEditor.currentParamManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(
		        PARAM_LOCAL_LPF_FREQ)) {
			numericDriver.setText("Off");
		}
		else patched_param::IntegerNonFM::drawValue();
	}
#endif
};
} // namespace menu_item::filter
