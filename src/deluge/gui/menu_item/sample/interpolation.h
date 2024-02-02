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
#include "model/sample/sample_controls.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::sample {
class Interpolation final : public Selection, public FormattedTitle {
public:
	Interpolation(l10n::String name, l10n::String title_format_str)
	    : Selection(name), FormattedTitle(title_format_str) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	void readCurrentValue() override { this->setValue(soundEditor.currentSampleControls->interpolationMode); }

	void writeCurrentValue() override {
		soundEditor.currentSampleControls->interpolationMode = this->getValue<InterpolationMode>();
	}

	deluge::vector<std::string_view> getOptions() override {
		return {l10n::getView(l10n::String::STRING_FOR_LINEAR), l10n::getView(l10n::String::STRING_FOR_SINC)};
	}

	bool isRelevant(Sound* sound, int32_t whichThing) override {
		if (sound == nullptr) {
			return true;
		}
		Source* source = &sound->sources[whichThing];
		return (sound->getSynthMode() == SynthMode::SUBTRACTIVE
		        && ((source->oscType == OscType::SAMPLE && source->hasAtLeastOneAudioFileLoaded())
		            || source->oscType == OscType::INPUT_L || source->oscType == OscType::INPUT_R
		            || source->oscType == OscType::INPUT_STEREO));
	}
};
} // namespace deluge::gui::menu_item::sample
