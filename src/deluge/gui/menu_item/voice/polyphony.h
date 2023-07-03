#pragma once

#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "model/drum/kit.h"
#include "gui/menu_item/selection.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::voice {
class Polyphony final : public Selection {
public:
	Polyphony(char const* newName = NULL) : Selection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->polyphonic; }
	void writeCurrentValue() {

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {

			Kit* kit = (Kit*)currentSong->currentClip->output;

			for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
				if (thisDrum->type == DRUM_TYPE_SOUND) {
					SoundDrum* soundDrum = (SoundDrum*)thisDrum;
					soundDrum->polyphonic = soundEditor.currentValue;
				}
			}
		}

		// Or, the normal case of just one sound
		else {
			soundEditor.currentSound->polyphonic = soundEditor.currentValue;
		}
	}

	char const** getOptions() {
		static char const* options[] = {"Auto", "Polyphonic", "Monophonic", "Legato", "Choke", NULL};
		return options;
	}

	int getNumOptions() { // Hack-ish way of hiding the "choke" option when not editing a Kit
		if (soundEditor.editingKit()) return NUM_POLYPHONY_TYPES;
		else return NUM_POLYPHONY_TYPES - 1;
	}

	bool usesAffectEntire() { return true; }
};
} // namespace menu_item::voice
