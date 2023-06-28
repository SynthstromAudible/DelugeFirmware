#pragma once
#include "model/clip/clip.h"
#include "gui/views/instrument_clip_view.h"
#include "model/drum/drum.h"
#include "model/drum/kit.h"
#include "selection.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::sample {

class Repeat final : public Selection {
public:
	Repeat(char const* newName = NULL) : Selection(newName) {}
	bool usesAffectEntire() { return true; }
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSource->repeatMode; }
	void writeCurrentValue() {

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {

			Kit* kit = (Kit*)currentSong->currentClip->output;

			for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
				if (thisDrum->type == DRUM_TYPE_SOUND) {
					SoundDrum* soundDrum = (SoundDrum*)thisDrum;
					Source* source = &soundDrum->sources[soundEditor.currentSourceIndex];

					// Automatically switch pitch/speed independence on / off if stretch-to-note-length mode is selected
					if (soundEditor.currentValue == SAMPLE_REPEAT_STRETCH) {
						soundDrum->unassignAllVoices();
						source->sampleControls.pitchAndSpeedAreIndependent = true;
					}
					else if (source->repeatMode == SAMPLE_REPEAT_STRETCH) {
						soundDrum->unassignAllVoices();
						soundEditor.currentSource->sampleControls.pitchAndSpeedAreIndependent = false;
					}

					source->repeatMode = soundEditor.currentValue;
				}
			}
		}

		// Or, the normal case of just one sound
		else {
			// Automatically switch pitch/speed independence on / off if stretch-to-note-length mode is selected
			if (soundEditor.currentValue == SAMPLE_REPEAT_STRETCH) {
				soundEditor.currentSound->unassignAllVoices();
				soundEditor.currentSource->sampleControls.pitchAndSpeedAreIndependent = true;
			}
			else if (soundEditor.currentSource->repeatMode == SAMPLE_REPEAT_STRETCH) {
				soundEditor.currentSound->unassignAllVoices();
				soundEditor.currentSource->sampleControls.pitchAndSpeedAreIndependent = false;
			}

			soundEditor.currentSource->repeatMode = soundEditor.currentValue;
		}

		// We need to re-render all rows, because this will have changed whether Note tails are displayed. Probably just one row, but we don't know which
		uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
	}
	char const** getOptions() {
		static char const* options[] = {"CUT", "ONCE", "LOOP", "STRETCH", NULL};
		return options;
	}
	int getNumOptions() { return NUM_REPEAT_MODES; }
};

} // namespace menu_item::sample
