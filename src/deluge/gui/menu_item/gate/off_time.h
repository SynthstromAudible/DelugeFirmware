#pragma once
#include "gui/menu_item/decimal.h"
#include "gui/ui/sound_editor.h"
#include "processing/engines/cv_engine.h"

namespace menu_item::gate {
class OffTime final : public Decimal {
public:
	using Decimal::Decimal;
	int getMinValue() const { return 1; }
	int getMaxValue() const { return 100; }
	int getNumDecimalPlaces() const { return 1; }
	int getDefaultEditPos() { return 1; }
	void readCurrentValue() { soundEditor.currentValue = cvEngine.minGateOffTime; }
	void writeCurrentValue() { cvEngine.minGateOffTime = soundEditor.currentValue; }
};
} // namespace menu_item::gate
