#pragma once
#include "modulation/patch/patch_cable_set.h"
#include "gui/menu_item/patched_param/integer_non_fm.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::filter {

class HPFFreq final : public patched_param::IntegerNonFM {
public:
	HPFFreq(char const* newName = 0, int newP = 0) : patched_param::IntegerNonFM(newName, newP) {}
#if !HAVE_OLED
	void drawValue() {
		if (soundEditor.currentValue == 0
		    && !soundEditor.currentParamManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(
		        PARAM_LOCAL_HPF_FREQ)) {
			numericDriver.setText("OFF");
		}
		else patched_param::IntegerNonFM::drawValue();
	}
#endif
};
}
