#pragma once
#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "model/drum/kit.h"
#include "gui/menu_item/selection.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::sample {
class PitchSpeed final : public Selection {
public:
	PitchSpeed(char const* newName = NULL) : Selection(newName) {}

  bool usesAffectEntire() { return true; }

  void readCurrentValue() {
		soundEditor.currentValue = soundEditor.currentSampleControls->pitchAndSpeedAreIndependent;
	}

  void writeCurrentValue() {
		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {

			Kit* kit = (Kit*)currentSong->currentClip->output;

			for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
				if (thisDrum->type == DRUM_TYPE_SOUND) {
					SoundDrum* soundDrum = (SoundDrum*)thisDrum;
					Source* source = &soundDrum->sources[soundEditor.currentSourceIndex];

					source->sampleControls.pitchAndSpeedAreIndependent = soundEditor.currentValue;
				}
			}
		}

		// Or, the normal case of just one sound
		else {
			soundEditor.currentSampleControls->pitchAndSpeedAreIndependent = soundEditor.currentValue;
		}
	}

  char const** getOptions() {
		static char const* options[] = {"Linked", "Independent", NULL};
		return options;
	}
};
} // namespace menu_item::sample
