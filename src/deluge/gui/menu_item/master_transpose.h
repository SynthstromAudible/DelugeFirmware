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
#include "gui/menu_item/transpose.h"
#include "gui/menu_item/value_scaling.h"
#include "gui/ui/sound_editor.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item {
class MasterTranspose final : public Transpose {
public:
	MasterTranspose(l10n::String name, l10n::String title)
	    : Transpose(name, title, deluge::modulation::params::LOCAL_PITCH_ADJUST) {}

	bool usesAffectEntire() override { return true; }
	void readCurrentValue() override {
		this->setValue(
		    computeCurrentValueForTranspose(soundEditor.currentSound->transpose, soundEditor.currentSound->cents));
	}
	void writeCurrentValue() override {

		int32_t transpose, cents;
		computeFinalValuesForTranspose(this->getValue(), &transpose, &cents);

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);

					soundDrum->transpose = transpose;
					soundDrum->set_cents(cents);

					char modelStackMemoryForSoundDrum[MODEL_STACK_MAX_SIZE];
					ModelStackWithSoundFlags* modelStackForSoundDrum =
					    getModelStackFromSoundDrum(modelStackMemoryForSoundDrum, soundDrum)->addSoundFlags();
					soundDrum->recalculateAllVoicePhaseIncrements(modelStackForSoundDrum);
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			soundEditor.currentSound->transpose = transpose;
			soundEditor.currentSound->set_cents(cents);

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();
			soundEditor.currentSound->recalculateAllVoicePhaseIncrements(modelStack);
		}
	}
	void getColumnLabel(StringBuf& label) override {
		return label.append(l10n::get(l10n::String::STRING_FOR_TRANSPOSE));
	}
};
} // namespace deluge::gui::menu_item
