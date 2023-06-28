#pragma once
#include "gui/menu_item/selection.h"
#include "processing/sound/sound.h"

namespace menu_item::sample {
class Selection : public menu_item::Selection {
public:
	Selection(char const* newName = NULL) : menu_item::Selection(newName) {}
	bool isRelevant(Sound* sound, int whichThing) {
		if (!sound) {
			return true; // For AudioClips
		}

		Source* source = &sound->sources[whichThing];
		return (sound->getSynthMode() == SYNTH_MODE_SUBTRACTIVE && source->oscType == OSC_TYPE_SAMPLE
		        && source->hasAtLeastOneAudioFileLoaded());
	}
};
} // namespace menu_item::sample
