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

namespace deluge::gui::menu_item::osc {
class Type final : public Selection, public FormattedTitle {
public:
	Type(l10n::String name, l10n::String title_format_str) : Selection(name), FormattedTitle(title_format_str){};
	void beginSession(MenuItem* navigatedBackwardFrom) override { Selection::beginSession(navigatedBackwardFrom); }

	void readCurrentValue() override { this->setValue(soundEditor.currentSource->oscType); }
	void writeCurrentValue() override {

		OscType oldValue = soundEditor.currentSource->oscType;
		auto newValue = this->getValue<OscType>();

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

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	deluge::vector<std::string_view> getOptions() override {
		using enum l10n::String;
		deluge::vector<std::string_view> options = {
		    l10n::getView(STRING_FOR_SINE),          //<
		    l10n::getView(STRING_FOR_TRIANGLE),      //<
		    l10n::getView(STRING_FOR_SQUARE),        //<
		    l10n::getView(STRING_FOR_ANALOG_SQUARE), //<
		    l10n::getView(STRING_FOR_SAW),           //<
		    l10n::getView(STRING_FOR_ANALOG_SAW),    //<
		    l10n::getView(STRING_FOR_WAVETABLE),     //<
		    l10n::getView(STRING_FOR_SAMPLE),        //<
		    l10n::getView(STRING_FOR_INPUT_LEFT),    //<
		    l10n::getView(STRING_FOR_INPUT_RIGHT),   //<
		    l10n::getView(STRING_FOR_INPUT_STEREO),  //<
		};
		options[8] = ((AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn)) //<
		                 ? l10n::getView(STRING_FOR_INPUT_LEFT)
		                 : l10n::getView(STRING_FOR_INPUT);

		if (soundEditor.currentSound->getSynthMode() == SynthMode::RINGMOD) {
			return {options.begin(), options.begin() + kNumOscTypesRingModdable};
		}
		if (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn) {
			return {options.begin(), options.begin() + kNumOscTypes};
		}
		return {options.begin(), options.begin() + kNumOscTypes - 2};
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		Sound* sound = static_cast<Sound*>(modControllable);
		return (sound->getSynthMode() != SynthMode::FM);
	}
};

} // namespace deluge::gui::menu_item::osc
