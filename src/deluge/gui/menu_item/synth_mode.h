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
#include "gui/ui/sound_editor.h"
#include "gui/views/view.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "util/misc.h"

namespace deluge::gui::menu_item {
class SynthMode final : public Selection<kNumSynthModes> {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(soundEditor.currentSound->synthMode); }
	void writeCurrentValue() override {
		soundEditor.currentSound->setSynthMode(this->getValue<::SynthMode>(), currentSong);
		view.setKnobIndicatorLevels();
	}

	static_vector<std::string_view, capacity()> getOptions() override {
		return {l10n::getView(l10n::String::STRING_FOR_SUBTRACTIVE), l10n::getView(l10n::String::STRING_FOR_FM),
		        l10n::getView(l10n::String::STRING_FOR_RINGMOD)};
	}

	bool isRelevant(Sound* sound, int32_t whichThing) override {
		return (sound->sources[0].oscType <= kLastRingmoddableOscType
		        && sound->sources[1].oscType <= kLastRingmoddableOscType);
	}
};
} // namespace deluge::gui::menu_item
