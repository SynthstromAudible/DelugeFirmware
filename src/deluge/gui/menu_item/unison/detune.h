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
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::unison {
class Detune final : public Integer {
public:
	using Integer::Integer;

	void readCurrentValue() override { this->setValue(soundEditor.currentSound->unisonDetune); }
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

					soundDrum->setUnisonDetune(current_value, modelStackForSoundDrum);
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();

			soundEditor.currentSound->setUnisonDetune(current_value, modelStack);
		}
	}
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxUnisonDetune; }

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		oled_canvas::Canvas& image = OLED::main;

		const float norm = normalize(getValue());

		for (int i = 0; i < 3; ++i) {
			constexpr int32_t line_spacing = 5;
			constexpr int32_t max_y_offset = 4;

			const int32_t y = slot.start_y + kHorizontalMenuSlotYOffset + i * line_spacing;
			const int32_t x0 = slot.start_x + 6;
			const int32_t x1 = slot.start_x + slot.width - 7;

			if (i == 0) {
				// Top line
				const int32_t offset = static_cast<int32_t>(max_y_offset * norm * 0.30f);
				image.drawLine(x0, y, x1, y + offset);
			}
			else if (i == 1) {
				// Middle line
				const int32_t offset = static_cast<int32_t>(max_y_offset * norm * 0.5f);
				image.drawLine(x0, y - offset, x1, y + offset);

				if (norm > 0.7 && norm < 1) {
					image.clearPixel(x0, y - offset);
					image.clearPixel(x1, y + offset);
					image.drawPixel(x0, y - offset - 1);
					image.drawPixel(x1, y + offset + 1);
				}
			}
			else if (i == 2) {
				// Bottom line
				int32_t offset = y - static_cast<int32_t>(max_y_offset * norm * 0.8f);
				if (norm > 0 && offset == y) {
					offset -= 1;
				}

				image.drawLine(x0, offset, x1 - 8, y);
				image.drawHorizontalLine(y, x1 - 8, x1);
			}
		}
	}
};
} // namespace deluge::gui::menu_item::unison
