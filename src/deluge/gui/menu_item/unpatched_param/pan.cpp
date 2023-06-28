#include "pan.h"

#include <cmath>
#include <cstring>
#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"

extern "C" {
#include "util/cfunctions.h"
}

namespace menu_item::unpatched_param {

void Pan::drawValue() {    // TODO: should really combine this with the "patched" version
	uint8_t drawDot = 255; //soundEditor.doesParamHaveAnyCables(getP()) ? 3 : 255;
	char buffer[5];
	intToString(std::abs(soundEditor.currentValue), buffer, 1);
	if (soundEditor.currentValue < 0) {
		strcat(buffer, "L");
	}
	else if (soundEditor.currentValue > 0) {
		strcat(buffer, "R");
	}
	numericDriver.setText(buffer, true, drawDot);
}

int32_t Pan::getFinalValue() {
	if (soundEditor.currentValue == 32) {
		return 2147483647;
	}
	else if (soundEditor.currentValue == -32) {
		return -2147483648;
	}
	else {
		return ((int32_t)soundEditor.currentValue * 33554432 * 2);
	}
}

void Pan::readCurrentValue() {
	soundEditor.currentValue =
	    ((int64_t)soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP()) * 64 + 2147483648) >> 32;
}

} // namespace menu_item::unpatched_param
