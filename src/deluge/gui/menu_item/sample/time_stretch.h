#pragma once
#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "gui/menu_item/integer.h"
#include "processing/sound/sound_drum.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/kit.h"
#include "model/song/song.h"

namespace menu_item::sample {
class TimeStretch final : public Integer {
public:
	TimeStretch(char const* newName = NULL) : Integer(newName) {}
	bool usesAffectEntire() { return true; }
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSource->timeStretchAmount; }
	void writeCurrentValue() {

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {

			Kit* kit = (Kit*)currentSong->currentClip->output;

			for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
				if (thisDrum->type == DRUM_TYPE_SOUND) {
					SoundDrum* soundDrum = (SoundDrum*)thisDrum;
					Source* source = &soundDrum->sources[soundEditor.currentSourceIndex];

					source->timeStretchAmount = soundEditor.currentValue;
				}
			}
		}

		// Or, the normal case of just one sound
		else {
			soundEditor.currentSource->timeStretchAmount = soundEditor.currentValue;
		}
	}
	int getMinValue() const { return -48; }
	int getMaxValue() const { return 48; }
	bool isRelevant(Sound* sound, int whichThing) {
		Source* source = &sound->sources[whichThing];
		return (sound->getSynthMode() == SYNTH_MODE_SUBTRACTIVE && source->oscType == OSC_TYPE_SAMPLE);
	}
};
} // namespace menu_item::sample
