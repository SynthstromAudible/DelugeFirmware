#pragma once
#include "gui/menu_item/bend_range.h"
#include "modulation/params/param_set.h"
#include "gui/ui/sound_editor.h"
#include "storage/flash_storage.h"
#include "modulation/params/param_manager.h"

namespace menu_item::bend_range {
class PerFinger final : public BendRange {
public:
	using BendRange::BendRange;
	void readCurrentValue() {
		ExpressionParamSet* expressionParams =
		    soundEditor.currentParamManager->getOrCreateExpressionParamSet(soundEditor.editingKit());
		soundEditor.currentValue = expressionParams ? expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL]
		                                            : FlashStorage::defaultBendRange[BEND_RANGE_FINGER_LEVEL];
	}
	void writeCurrentValue() {
		ExpressionParamSet* expressionParams =
		    soundEditor.currentParamManager->getOrCreateExpressionParamSet(soundEditor.editingKit());
		if (expressionParams) expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL] = soundEditor.currentValue;
	}
	bool isRelevant(Sound* sound, int whichThing) {
		return soundEditor.navigationDepth == 1 || soundEditor.editingKit();
	}
};
}
