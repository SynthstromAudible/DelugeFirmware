#include "pan.h"

#include <cstring>
#include <cmath>
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "gui/ui/sound_editor.h"

extern "C" {
#include "util/cfunctions.h"
}

namespace menu_item::patched_param {
#if !HAVE_OLED
void Pan::drawValue() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(getP());
	uint8_t drawDot =
	    soundEditor.currentParamManager->getPatchCableSet()->isAnySourcePatchedToParamVolumeInspecific(paramDescriptor)
	        ? 3
	        : 255;
	char buffer[5];
	intToString(std::abs(soundEditor.currentValue), buffer, 1);
	if (soundEditor.currentValue < 0) strcat(buffer, "L");
	else if (soundEditor.currentValue > 0) strcat(buffer, "R");
	numericDriver.setText(buffer, true, drawDot);
}
#endif

int32_t Pan::getFinalValue() {
	if (soundEditor.currentValue == 32) return 2147483647;
	else if (soundEditor.currentValue == -32) return -2147483648;
	else return ((int32_t)soundEditor.currentValue * 33554432 * 2);
}

void Pan::readCurrentValue() {
	soundEditor.currentValue =
	    ((int64_t)soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP()) * 64 + 2147483648) >> 32;
}
} // namespace menu_item::patched_param
