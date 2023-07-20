/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once
#include "definitions_cxx.hpp"
#include "gui/menu_item/selection.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "gui/ui/sound_editor.h"
#include "processing/engines/audio_engine.h"
#include "processing/source.h"
#include "util/comparison.h"
#include "util/misc.h"

extern char oscTypeTitle[];
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
		Selection::beginSession(navigatedBackwardFrom);
	}
#endif
	void readCurrentValue() { soundEditor.currentValue = util::to_underlying(soundEditor.currentSource->oscType); }
	void writeCurrentValue() {

		OscType oldValue = soundEditor.currentSource->oscType;
		auto newValue = static_cast<OscType>(soundEditor.currentValue);

		auto needs_unassignment = {OscType::INPUT_L, OscType::INPUT_R, OscType::INPUT_STEREO, OscType::SAMPLE,

		                           // Haven't actually really determined if this needs to be here - maybe not?
		                           OscType::WAVETABLE};

		if (util::one_of(oldValue, needs_unassignment) || util::one_of(newValue, needs_unassignment)) {
			soundEditor.currentSound->unassignAllVoices();
		}

		soundEditor.currentSource->setOscType(newValue);
		if (oldValue == OscType::SQUARE || newValue == OscType::SQUARE) {
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
		inLText[5] = ((AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn)) ? ' ' : 0;
#else
		static char inLText[4] = "INL";
		static char const* options[] = {"SINE",      "TRIANGLE", "SQUARE", "ASQUARE", "SAW", "ASAW",
		                                "Wavetable", "SAMPLE",   inLText,  "INR",     "INLR"};
		inLText[2] = ((AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn)) ? 'L' : 0;
#endif
		return options;
	}

	int getNumOptions() {
		if (soundEditor.currentSound->getSynthMode() == SynthMode::RINGMOD) {
			return kNumOscTypesRingModdable;
		}
		else if (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn) {
			return kNumOscTypes;
		}
		else {
			return kNumOscTypes - 2;
		}
	}
	bool isRelevant(Sound* sound, int whichThing) { return (sound->getSynthMode() != SynthMode::FM); }
};

} // namespace menu_item::osc
