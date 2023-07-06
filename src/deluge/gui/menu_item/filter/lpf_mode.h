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
#include "model/mod_controllable/mod_controllable_audio.h"
#include "gui/menu_item/selection.h"
#include "processing/sound/sound.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::filter {
class LPFMode final : public Selection {
public:
	LPFMode(char const* newName = NULL) : Selection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentModControllable->lpfMode; }
	void writeCurrentValue() { soundEditor.currentModControllable->lpfMode = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {"12dB", "24dB", "Drive", "SVF", NULL};
		return options;
	}
	int getNumOptions() { return NUM_LPF_MODES; }
	bool isRelevant(Sound* sound, int whichThing) { return (!sound || sound->synthMode != SYNTH_MODE_FM); }
};
} // namespace menu_item::filter
