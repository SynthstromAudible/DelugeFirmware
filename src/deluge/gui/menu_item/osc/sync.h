#pragma once
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace menu_item::osc {
class Sync final : public Selection {
public:
	Sync(char const* newName = NULL) : Selection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->oscillatorSync; }
	void writeCurrentValue() { soundEditor.currentSound->oscillatorSync = soundEditor.currentValue; }
	bool isRelevant(Sound* sound, int whichThing) {
		return (whichThing == 1 && sound->synthMode != SYNTH_MODE_FM && sound->sources[0].oscType != OSC_TYPE_SAMPLE
		        && sound->sources[1].oscType != OSC_TYPE_SAMPLE);
	}
};

} // namespace menu_item::osc
