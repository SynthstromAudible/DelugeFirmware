#pragma once
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"
#include "model/song/song.h"
#include "gui/views/view.h"

namespace menu_item {
class SynthMode final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->synthMode; }
	void writeCurrentValue() {
		soundEditor.currentSound->setSynthMode(soundEditor.currentValue, currentSong);
		view.setKnobIndicatorLevels();
	}
	char const** getOptions() {
		static char const* options[] = {"Subtractive", "FM", "Ringmod", NULL};
		return options;
	}
	int getNumOptions() { return 3; }
	bool isRelevant(Sound* sound, int whichThing) {
		return (sound->sources[0].oscType < NUM_OSC_TYPES_RINGMODDABLE
		        && sound->sources[1].oscType < NUM_OSC_TYPES_RINGMODDABLE);
	}
};
}
