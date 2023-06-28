#pragma once
#include "gui/menu_item/lfo/shape.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace menu_item::lfo::global {

class Type final : public Shape {
public:
	using Shape::Shape;

	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->lfoGlobalWaveType; }
	void writeCurrentValue() { soundEditor.currentSound->setLFOGlobalWave(soundEditor.currentValue); }
};

} // namespace menu_item::lfo::global
