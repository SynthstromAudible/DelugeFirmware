#include "integer.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_set.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::patched_param {
void Integer::readCurrentValue() {
	soundEditor.currentValue =
	    (((int64_t)soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP()) + 2147483648) * 50
	     + 2147483648)
	    >> 32;
}

void Integer::writeCurrentValue() {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* modelStack = getModelStack(modelStackMemory);
	modelStack->autoParam->setCurrentValueInResponseToUserInput(getFinalValue(), modelStack);

	//((ParamManagerBase*)soundEditor.currentParamManager)->setPatchedParamValue(getP(), getFinalValue(), 0xFFFFFFFF, 0, soundEditor.currentSound, currentSong, currentSong->currentClip, true, true);
}

int32_t Integer::getFinalValue() {
	if (soundEditor.currentValue == 25) return 0;
	else return (uint32_t)soundEditor.currentValue * 85899345 - 2147483648;
}

} // namespace menu_item::patched_param
