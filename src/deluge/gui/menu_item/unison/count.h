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
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "model/model_stack.h"
#include "processing/sound/sound.h"
#include "stereoSpread.h"

namespace deluge::gui::menu_item::unison {

class Count : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->setValue(soundEditor.currentSound->numUnison); }
	void writeCurrentValue() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();
		soundEditor.currentSound->setNumUnison(this->getValue(), modelStack);
	}
	[[nodiscard]] int32_t getMinValue() const override { return 1; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxNumVoicesUnison; }
};

class CountToStereoSpread final : public Count {
public:
	using Count::Count;
	MenuItem* selectButtonPress() override { return &unison::stereoSpreadMenu; }
};

} // namespace deluge::gui::menu_item::unison
