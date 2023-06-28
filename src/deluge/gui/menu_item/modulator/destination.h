#pragma once
#include "gui/menu_item/selection.h"
#include "processing/sound/sound.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::modulator {
class Destination final : public Selection {
public:
	Destination(char const* newName = NULL) : Selection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->modulator1ToModulator0; }
	void writeCurrentValue() { soundEditor.currentSound->modulator1ToModulator0 = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {"Carriers", HAVE_OLED ? "Modulator 1" : "MOD1", NULL};
		return options;
	}
	bool isRelevant(Sound* sound, int whichThing) { return (whichThing == 1 && sound->synthMode == SYNTH_MODE_FM); }
};

}
