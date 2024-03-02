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
#include "gui/menu_item/menu_item_with_cc_learning.h"
#include "gui/menu_item/patched_param.h"
#include "gui/ui/sound_editor.h"
#include "menu_item_with_cc_learning.h"
#include "model/model_stack.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item {
class MasterTranspose final : public Integer, public PatchedParam {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->setValue(soundEditor.currentSound->transpose); }
	void writeCurrentValue() override {
		soundEditor.currentSound->transpose = this->getValue();
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();
		soundEditor.currentSound->recalculateAllVoicePhaseIncrements(modelStack);
	}
	MenuItem* selectButtonPress() override { return PatchedParam::selectButtonPress(); }
	uint8_t shouldDrawDotOnName() override { return PatchedParam::shouldDrawDotOnName(); }
	uint8_t getPatchedParamIndex() override { return deluge::modulation::params::LOCAL_PITCH_ADJUST; }
	uint8_t getP() override { return deluge::modulation::params::LOCAL_PITCH_ADJUST; }
	uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) override {
		return PatchedParam::shouldBlinkPatchingSourceShortcut(s, colour);
	}
	MenuItem* patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive = false) override {
		return PatchedParam::patchingSourceShortcutPress(s, previousPressStillActive);
	}

	void drawValue() override { display->setTextAsNumber(this->getValue(), shouldDrawDotOnName()); }

	void unlearnAction() override { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() override { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDIDevice* fromDevice, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) override {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};

	[[nodiscard]] int32_t getMinValue() const override { return -96; }
	[[nodiscard]] int32_t getMaxValue() const override { return 96; }
};
} // namespace deluge::gui::menu_item
