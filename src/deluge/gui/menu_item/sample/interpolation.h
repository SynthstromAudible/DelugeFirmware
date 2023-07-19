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
#include "gui/menu_item/formatted_title.h"
#include "model/sample/sample_controls.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::sample {
class Interpolation final : public Selection<2>, public FormattedTitle {
public:
	Interpolation(const string& name, const string& title_format_str)
	    : Selection(name), FormattedTitle(title_format_str) {}

	[[nodiscard]] const string& getTitle() const override { return FormattedTitle::title(); }

	void readCurrentValue() override { this->value_ = soundEditor.currentSampleControls->interpolationMode; }

	void writeCurrentValue() override { soundEditor.currentSampleControls->interpolationMode = this->value_; }

	static_vector<string, capacity()> getOptions() override { return {"Linear", "Sinc"}; }

	bool isRelevant(Sound* sound, int whichThing) override {
		if (sound == nullptr) {
			return true;
		}
		Source* source = &sound->sources[whichThing];
		return (sound->getSynthMode() == SYNTH_MODE_SUBTRACTIVE
		        && ((source->oscType == OSC_TYPE_SAMPLE && source->hasAtLeastOneAudioFileLoaded())
		            || source->oscType == OSC_TYPE_INPUT_L || source->oscType == OSC_TYPE_INPUT_R
		            || source->oscType == OSC_TYPE_INPUT_STEREO));
	}
};
} // namespace deluge::gui::menu_item::sample
