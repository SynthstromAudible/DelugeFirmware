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
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"
#include "model/song/song.h"
#include "gui/views/view.h"

namespace deluge::gui::menu_item {
class SynthMode final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { soundEditor.currentValue = soundEditor.currentSound->synthMode; }
	void writeCurrentValue() override {
		soundEditor.currentSound->setSynthMode(soundEditor.currentValue, currentSong);
		view.setKnobIndicatorLevels();
	}
	Sized<char const**> getOptions() override {
		static char const* options[] = {"Subtractive", "FM", "Ringmod"};
		return {options, 3};
	}

	bool isRelevant(Sound* sound, int whichThing) override {
		return (sound->sources[0].oscType < NUM_OSC_TYPES_RINGMODDABLE
		        && sound->sources[1].oscType < NUM_OSC_TYPES_RINGMODDABLE);
	}
};
} // namespace deluge::gui::menu_item
