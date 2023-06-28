#pragma once
#include "gui/menu_item/bend_range.h"
#include "gui/ui/sound_editor.h"
#include "modulation/params/param_set.h"
#include "storage/flash_storage.h"
#include "modulation/params/param_manager.h"

namespace menu_item::bend_range {
class Main final : public BendRange {
public:
	using BendRange::BendRange;
	void readCurrentValue() {
		ExpressionParamSet* expressionParams =
		    soundEditor.currentParamManager->getOrCreateExpressionParamSet(soundEditor.editingKit());
		soundEditor.currentValue = expressionParams ? expressionParams->bendRanges[BEND_RANGE_MAIN]
		                                            : FlashStorage::defaultBendRange[BEND_RANGE_MAIN];
	}
	void writeCurrentValue() {
		ExpressionParamSet* expressionParams =
		    soundEditor.currentParamManager->getOrCreateExpressionParamSet(soundEditor.editingKit());
		if (expressionParams) expressionParams->bendRanges[BEND_RANGE_MAIN] = soundEditor.currentValue;
	}
};
}
