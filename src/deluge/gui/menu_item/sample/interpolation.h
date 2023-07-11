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
#include "model/sample/sample_controls.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::sample {
class Interpolation final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override {
		soundEditor.currentValue = soundEditor.currentSampleControls->interpolationMode;
	}
	void writeCurrentValue() override {
		soundEditor.currentSampleControls->interpolationMode = soundEditor.currentValue;
	}
	Sized<char const**> getOptions() override {
		static char const* options[] = {"Linear", "Sinc"};
		return {options, 2};
	}
	bool isRelevant(Sound* sound, int whichThing) override {
		if (!sound) {
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
