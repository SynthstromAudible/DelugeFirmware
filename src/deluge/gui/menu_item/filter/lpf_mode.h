#pragma once
#include "model/mod_controllable/mod_controllable_audio.h"
#include "gui/menu_item/selection.h"
#include "processing/sound/sound.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::filter {
class LPFMode final : public Selection {
public:
	LPFMode(char const* newName = NULL) : Selection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentModControllable->lpfMode; }
	void writeCurrentValue() { soundEditor.currentModControllable->lpfMode = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {"12dB", "24dB", "Drive", "SVF", NULL};
		return options;
	}
	int getNumOptions() { return NUM_LPF_MODES; }
	bool isRelevant(Sound* sound, int whichThing) { return (!sound || sound->synthMode != SYNTH_MODE_FM); }
};
} // namespace menu_item::filter
