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
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "util/comparison.h"
#include "util/misc.h"

namespace deluge::gui::menu_item::osc {
class Type final : public Selection<kNumOscTypes>, public FormattedTitle {
public:
	Type(const string& name, const fmt::format_string<int32_t>& title_format_str)
	    : Selection(name), FormattedTitle(title_format_str){};
#if HAVE_OLED
	void beginSession(MenuItem* navigatedBackwardFrom) override {
		Selection::beginSession(navigatedBackwardFrom);
	}
#endif
	void readCurrentValue() override {
		this->set_value(soundEditor.currentSource->oscType);
	}
	void writeCurrentValue() override {

		OscType oldValue = soundEditor.currentSource->oscType;
		auto newValue = this->get_value<OscType>();

		auto needs_unassignment = {
		    OscType::INPUT_L,
		    OscType::INPUT_R,
		    OscType::INPUT_STEREO,
		    OscType::SAMPLE,

		    // Haven't actually really determined if this needs to be here - maybe not?
		    OscType::WAVETABLE,
		};

		if (util::one_of(oldValue, needs_unassignment) || util::one_of(newValue, needs_unassignment)) {
			soundEditor.currentSound->unassignAllVoices();
		}

		soundEditor.currentSource->setOscType(newValue);
		if (oldValue == OscType::SQUARE || newValue == OscType::SQUARE) {
			soundEditor.currentSound->setupPatchingForAllParamManagers(currentSong);
		}
	}

	[[nodiscard]] std::string_view getTitle() const override {
		return FormattedTitle::title();
	}

	//char const** getOptions() { static char const* options[] = {"SINE", "TRIANGLE", "SQUARE", "SAW", "MMS1", "SUB1", "SAMPLE", "INL", "INR", "INLR", "SQ50", "SQ02", "SQ01", "SUB2", "SQ20", "SA50", "S101", "S303", "MMS2", "MMS3", "TABLE"}; return options; }
	static_vector<string, capacity()> getOptions() override {
		static_vector<string, capacity()> options = {
		    "SINE",
		    "TRIANGLE",
		    "SQUARE",
		    HAVE_OLED ? "Analog square" : "ASQUARE",
		    "Saw",
		    HAVE_OLED ? "Analog saw" : "ASAW",
		    "Wavetable",
		    "SAMPLE",
		    HAVE_OLED ? "Input (left)" : "INL",
		    HAVE_OLED ? "Input (right)" : "INR",
		    HAVE_OLED ? "Input (stereo)" : "INLR",
		};
#if HAVE_OLED
		options[8] = ((AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn)) ? "Input (left)" : "Input";
#else

		options[8] = ((AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn)) ? "INL" : "IN";
#endif

		if (soundEditor.currentSound->getSynthMode() == SynthMode::RINGMOD) {
			return {options.begin(), options.begin() + kNumOscTypesRingModdable};
		}
		if (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn) {
			return {options.begin(), options.begin() + kNumOscTypes};
		}
		return {options.begin(), options.begin() + kNumOscTypes - 2};
	}

	bool isRelevant(Sound* sound, int32_t whichThing) override {
		return (sound->getSynthMode() != SynthMode::FM);
	}
};

} // namespace deluge::gui::menu_item::osc
