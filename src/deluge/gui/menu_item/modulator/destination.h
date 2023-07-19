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
#include "processing/sound/sound.h"
#include "gui/ui/sound_editor.h"

namespace deluge::gui::menu_item::modulator {
class Destination final : public Selection<2> {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->value_ = soundEditor.currentSound->modulator1ToModulator0; }
	void writeCurrentValue() override { soundEditor.currentSound->modulator1ToModulator0 = this->value_; }
	static_vector<string, capacity()> getOptions() override {
		return {"Carriers", HAVE_OLED ? "Modulator 1" : "MOD1"};
	}
	bool isRelevant(Sound* sound, int whichThing) override {
		return (whichThing == 1 && sound->synthMode == SYNTH_MODE_FM);
	}
};

} // namespace deluge::gui::menu_item::modulator
