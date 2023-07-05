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

namespace menu_item::modulator {
class Destination final : public Selection {
public:
	Destination(char const* newName = NULL) : Selection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->modulator1ToModulator0; }
	void writeCurrentValue() { soundEditor.currentSound->modulator1ToModulator0 = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {"Carriers", HAVE_OLED ? "Modulator 1" : "MOD1", NULL};
		return options;
	}
	bool isRelevant(Sound* sound, int whichThing) { return (whichThing == 1 && sound->synthMode == SYNTH_MODE_FM); }
};

} // namespace menu_item::modulator
