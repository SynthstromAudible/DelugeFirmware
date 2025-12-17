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
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "stereoSpread.h"

namespace deluge::gui::menu_item::unison {

class Count : public Integer {
public:
	using Integer::Integer;

	void readCurrentValue() override { this->setValue(soundEditor.currentSound->numUnison); }
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		int32_t current_value = this->getValue();

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);

					char modelStackMemoryForSoundDrum[MODEL_STACK_MAX_SIZE];
					ModelStackWithSoundFlags* modelStackForSoundDrum =
					    getModelStackFromSoundDrum(modelStackMemoryForSoundDrum, soundDrum)->addSoundFlags();

					soundDrum->setNumUnison(current_value, modelStackForSoundDrum);
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();

			soundEditor.currentSound->setNumUnison(current_value, modelStack);
		}
	}
	[[nodiscard]] int32_t getMinValue() const override { return 1; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxNumVoicesUnison; }
	[[nodiscard]] bool showColumnLabel() const override { return false; }

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		DEF_STACK_STRING_BUF(paramValue, 2);
		paramValue.appendInt(getValue());
		OLED::main.drawStringCentered(paramValue, slot.start_x + 1, slot.start_y + kHorizontalMenuSlotYOffset + 3,
		                              kTextBigSpacingX, kTextBigSizeY, slot.width);
	}
};

class CountToStereoSpread final : public Count {
public:
	using Count::Count;
	MenuItem* selectButtonPress() override { return &unison::stereoSpreadMenu; }
};

} // namespace deluge::gui::menu_item::unison
