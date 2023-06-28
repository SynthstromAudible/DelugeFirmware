#pragma once
#include "definitions.h"
#include "gui/menu_item/selection.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "gui/ui/sound_editor.h"
#include "processing/engines/audio_engine.h"
#include "processing/source.h"
#include "util/comparison.h"
#include <initializer_list>

namespace menu_item::osc {
class Type final : public Selection {
public:
	Type(char const* newName = NULL) : Selection(newName) {
#if HAVE_OLED
		basicTitle = oscTypeTitle;
#endif
	}
#if HAVE_OLED
	void beginSession(MenuItem* navigatedBackwardFrom) {
		oscTypeTitle[3] = '1' + soundEditor.currentSourceIndex;
		MenuItemSelection::beginSession(navigatedBackwardFrom);
	}
#endif
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSource->oscType; }
	void writeCurrentValue() {

		int oldValue = soundEditor.currentSource->oscType;
		int newValue = soundEditor.currentValue;

		auto needs_unassignment = {OSC_TYPE_INPUT_L, OSC_TYPE_INPUT_R, OSC_TYPE_INPUT_STEREO, OSC_TYPE_SAMPLE,

		                           // Haven't actually really determined if this needs to be here - maybe not?
		                           OSC_TYPE_WAVETABLE};

		if (util::one_of(oldValue, needs_unassignment) || util::one_of(newValue, needs_unassignment)) {
			soundEditor.currentSound->unassignAllVoices();
		}

		soundEditor.currentSource->setOscType(newValue);
		if (oldValue == OSC_TYPE_SQUARE || newValue == OSC_TYPE_SQUARE) {
			soundEditor.currentSound->setupPatchingForAllParamManagers(currentSong);
		}
	}

	//char const** getOptions() { static char const* options[] = {"SINE", "TRIANGLE", "SQUARE", "SAW", "MMS1", "SUB1", "SAMPLE", "INL", "INR", "INLR", "SQ50", "SQ02", "SQ01", "SUB2", "SQ20", "SA50", "S101", "S303", "MMS2", "MMS3", "TABLE"}; return options; }
	char const** getOptions() {
#if HAVE_OLED
		static char inLText[] = "Input (left)";
		static char const* options[] = {"SINE",  "TRIANGLE",      "SQUARE",         "Analog square",
		                                "Saw",   "Analog saw",    "Wavetable",      "SAMPLE",
		                                inLText, "Input (right)", "Input (stereo)", NULL};
		inLText[5] =
		    ((AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn || DELUGE_MODEL == DELUGE_MODEL_40_PAD)) ? ' '
		                                                                                                         : 0;
#else
		static char inLText[4] = "INL";
		static char const* options[] = {"SINE",      "TRIANGLE", "SQUARE", "ASQUARE", "SAW", "ASAW",
		                                "Wavetable", "SAMPLE",   inLText,  "INR",     "INLR"};
		inLText[2] =
		    ((AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn || DELUGE_MODEL == DELUGE_MODEL_40_PAD)) ? 'L'
		                                                                                                         : 0;
#endif
		return options;
	}

	int getNumOptions() {
		if (soundEditor.currentSound->getSynthMode() == SYNTH_MODE_RINGMOD) {
			return NUM_OSC_TYPES_RINGMODDABLE;
		}
		else if (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn || DELUGE_MODEL == DELUGE_MODEL_40_PAD) {
			return NUM_OSC_TYPES;
		}
		else {
			return NUM_OSC_TYPES - 2;
		}
	}
	bool isRelevant(Sound* sound, int whichThing) { return (sound->getSynthMode() != SYNTH_MODE_FM); }
};

} // namespace menu_item::osc
