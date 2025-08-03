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
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/source/transpose.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::modulator {

class Transpose final : public source::Transpose, public FormattedTitle {
public:
	Transpose(l10n::String name, l10n::String title_format_str, int32_t newP, uint8_t source_id)
	    : source::Transpose(name, newP, source_id), FormattedTitle(title_format_str, source_id + 1) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	void readCurrentValue() override {
		this->setValue(computeCurrentValueForTranspose(soundEditor.currentSound->modulatorTranspose[source_id_],
		                                               soundEditor.currentSound->modulatorCents[source_id_]));
	}

	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		int32_t transpose, cents;
		computeFinalValuesForTranspose(this->getValue(), &transpose, &cents);

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);

					// Note: we need to apply the same filtering as stated in the isRelevant() function
					if (soundDrum->getSynthMode() == SynthMode::FM) {
						char modelStackMemoryForSoundDrum[MODEL_STACK_MAX_SIZE];
						ModelStackWithSoundFlags* modelStackForSoundDrum =
						    getModelStackFromSoundDrum(modelStackMemoryForSoundDrum, soundDrum)->addSoundFlags();

						soundDrum->setModulatorTranspose(source_id_, transpose, modelStackForSoundDrum);
						soundDrum->setModulatorCents(source_id_, cents, modelStackForSoundDrum);
					}
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();

			soundEditor.currentSound->setModulatorTranspose(source_id_, transpose, modelStack);
			soundEditor.currentSound->setModulatorCents(source_id_, cents, modelStack);
		}
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		Sound* sound = static_cast<Sound*>(modControllable);
		return (sound->getSynthMode() == SynthMode::FM);
	}
};

} // namespace deluge::gui::menu_item::modulator
