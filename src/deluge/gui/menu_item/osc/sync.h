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

namespace menu_item::osc {
class Sync final : public Selection {
public:
	Sync(char const* newName = NULL) : Selection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentSound->oscillatorSync; }
	void writeCurrentValue() { soundEditor.currentSound->oscillatorSync = soundEditor.currentValue; }
	bool isRelevant(Sound* sound, int whichThing) {
		return (whichThing == 1 && sound->synthMode != SYNTH_MODE_FM && sound->sources[0].oscType != OSC_TYPE_SAMPLE
		        && sound->sources[1].oscType != OSC_TYPE_SAMPLE);
	}
};

} // namespace menu_item::osc
