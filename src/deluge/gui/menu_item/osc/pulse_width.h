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
#include "modulation/params/param_set.h"
#include "gui/menu_item/source/patched_param.h"
#include "processing/sound/sound.h"

namespace menu_item::osc {
class PulseWidth final : public menu_item::source::PatchedParam {
public:
	using menu_item::source::PatchedParam::PatchedParam;

	int32_t getFinalValue() { return (uint32_t)soundEditor.currentValue * (85899345 >> 1); }

	void readCurrentValue() {
		soundEditor.currentValue =
		    ((int64_t)soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP()) * 100 + 2147483648) >> 32;
	}

	bool isRelevant(Sound* sound, int whichThing) {
		if (sound->getSynthMode() == SYNTH_MODE_FM) {
			return false;
		}
		int oscType = sound->sources[whichThing].oscType;
		return (oscType != OSC_TYPE_SAMPLE && oscType != OSC_TYPE_INPUT_L && oscType != OSC_TYPE_INPUT_R
		        && oscType != OSC_TYPE_INPUT_STEREO);
	}
};

} // namespace menu_item::osc
