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
#include "gui/menu_item/bend_range.h"
#include "modulation/params/param_set.h"
#include "gui/ui/sound_editor.h"
#include "storage/flash_storage.h"
#include "modulation/params/param_manager.h"

namespace menu_item::bend_range {
class PerFinger final : public BendRange {
public:
	using BendRange::BendRange;
	void readCurrentValue() {
		ExpressionParamSet* expressionParams =
		    soundEditor.currentParamManager->getOrCreateExpressionParamSet(soundEditor.editingKit());
		soundEditor.currentValue = expressionParams ? expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL]
		                                            : FlashStorage::defaultBendRange[BEND_RANGE_FINGER_LEVEL];
	}
	void writeCurrentValue() {
		ExpressionParamSet* expressionParams =
		    soundEditor.currentParamManager->getOrCreateExpressionParamSet(soundEditor.editingKit());
		if (expressionParams) {
			expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL] = soundEditor.currentValue;
		}
	}
	bool isRelevant(Sound* sound, int whichThing) {
		return soundEditor.navigationDepth == 1 || soundEditor.editingKit();
	}
};
} // namespace menu_item::bend_range
