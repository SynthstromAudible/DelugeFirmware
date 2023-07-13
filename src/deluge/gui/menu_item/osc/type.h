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
#include "definitions.h"
#include "gui/menu_item/selection.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "gui/ui/sound_editor.h"
#include "processing/engines/audio_engine.h"
#include "processing/source.h"
#include "util/comparison.h"

extern char oscTypeTitle[];
namespace deluge::gui::menu_item::osc {
class Type final : public Selection<NUM_OSC_TYPES> {
public:
	using Selection::Selection;
#if HAVE_OLED
	void beginSession(MenuItem* navigatedBackwardFrom) {
		oscTypeTitle[3] = '1' + soundEditor.currentSourceIndex;
		Selection::beginSession(navigatedBackwardFrom);
	}
#endif
	void readCurrentValue() override {
		this->value_ = soundEditor.currentSource->oscType;
	}
	void writeCurrentValue() override {

		int oldValue = soundEditor.currentSource->oscType;
		int newValue = this->value_;

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
	static_vector<char const*, capacity()> getOptions() override {
		static_vector<char const*, capacity()> options = {"SINE",
		                                                  "TRIANGLE",
		                                                  "SQUARE",
		                                                  HAVE_OLED ? "Analog square" : "ASQUARE",
		                                                  "Saw",
		                                                  HAVE_OLED ? "Analog saw" : "ASAW",
		                                                  "Wavetable",
		                                                  "SAMPLE",
		                                                  HAVE_OLED ? "Input (left)" : "INL",
		                                                  HAVE_OLED ? "Input (right)" : "INR",
		                                                  HAVE_OLED ? "Input (stereo)" : "INLR"};
#if HAVE_OLED
		options[8] = ((AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn)) ? "Input (left)" : "Input";
#else

		options[8] = ((AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn)) ? "INL" : "IN";
#endif

		if (soundEditor.currentSound->getSynthMode() == SYNTH_MODE_RINGMOD) {
			return {options.begin(), options.begin() + NUM_OSC_TYPES_RINGMODDABLE};
		}
		if (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn) {
			return {options.begin(), options.begin() + NUM_OSC_TYPES};
		}
		return {options.begin(), options.begin() + NUM_OSC_TYPES - 2};
	}

	size_t getNumOptions() {
	}

	bool isRelevant(Sound* sound, int whichThing) override {
		return (sound->getSynthMode() != SYNTH_MODE_FM);
	}
};

} // namespace deluge::gui::menu_item::osc
