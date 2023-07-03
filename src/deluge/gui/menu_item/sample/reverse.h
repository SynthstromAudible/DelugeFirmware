#pragma once
#include "model/clip/clip.h"
#include "model/drum/kit.h"
#include "selection.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::sample {
class Reverse final : public Selection {
public:
	Reverse(char const* newName = NULL) : Selection(newName) {}
	bool usesAffectEntire() { return true; }
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSource->sampleControls.reversed; }
	void writeCurrentValue() {

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {

			Kit* kit = (Kit*)currentSong->currentClip->output;

			for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
				if (thisDrum->type == DRUM_TYPE_SOUND) {
					SoundDrum* soundDrum = (SoundDrum*)thisDrum;
					Source* source = &soundDrum->sources[soundEditor.currentSourceIndex];

					soundDrum->unassignAllVoices();
					source->setReversed(soundEditor.currentValue);
				}
			}
		}

		// Or, the normal case of just one sound
		else {
			soundEditor.currentSound->unassignAllVoices();
			soundEditor.currentSource->setReversed(soundEditor.currentValue);
		}
	}
};
} // namespace menu_item::sample
