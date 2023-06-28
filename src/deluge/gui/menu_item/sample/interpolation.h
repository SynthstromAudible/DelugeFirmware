#pragma once
#include "model/sample/sample_controls.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace menu_item::sample {
class Interpolation final : public Selection {
public:
	Interpolation(char const* newName = NULL) : Selection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSampleControls->interpolationMode; }
	void writeCurrentValue() { soundEditor.currentSampleControls->interpolationMode = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {"Linear", "Sinc", NULL};
		return options;
	}
	bool isRelevant(Sound* sound, int whichThing) {
		if (!sound) return true;
		Source* source = &sound->sources[whichThing];
		return (sound->getSynthMode() == SYNTH_MODE_SUBTRACTIVE
		        && ((source->oscType == OSC_TYPE_SAMPLE && source->hasAtLeastOneAudioFileLoaded())
		            || source->oscType == OSC_TYPE_INPUT_L || source->oscType == OSC_TYPE_INPUT_R
		            || source->oscType == OSC_TYPE_INPUT_STEREO));
	}
};
}
