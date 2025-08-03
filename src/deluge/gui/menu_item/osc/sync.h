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
#include "gui/menu_item/toggle.h"
#include "gui/ui/sound_editor.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::osc {
class Sync final : public Toggle {
public:
	using Toggle::Toggle;
	void readCurrentValue() override { this->setValue(soundEditor.currentSound->oscillatorSync); }
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		bool current_value = this->getValue();

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				// Note: we need to apply the same filtering as stated in the isRelevant() function
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					// Note: we need to apply the same filtering as stated in the isRelevant() function
					if (soundDrum->synthMode != SynthMode::FM && soundDrum->sources[0].oscType != OscType::SAMPLE
					    && soundDrum->sources[1].oscType != OscType::SAMPLE) {
						soundDrum->oscillatorSync = current_value;
					}
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			soundEditor.currentSound->oscillatorSync = current_value;
		}
	}
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		Sound* sound = static_cast<Sound*>(modControllable);
		return sound->synthMode != SynthMode::FM && sound->sources[0].oscType != OscType::SAMPLE
		       && sound->sources[1].oscType != OscType::SAMPLE;
	}

	void getColumnLabel(StringBuf& label) override { label.append(l10n::get(l10n::String::STRING_FOR_SYNC)); }
};

} // namespace deluge::gui::menu_item::osc
