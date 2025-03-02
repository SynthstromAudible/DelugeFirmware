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
#include "definitions_cxx.hpp"
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::midi::sound {

class OutputMidiNoteForDrum final : public Integer {
public:
	using Integer::Integer;
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMIDIValue; }
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return soundEditor.editingKitRow() && !soundEditor.editingNonAudioDrumRow();
	}
	void readCurrentValue() override {
		int32_t value = soundEditor.currentSound->outputMidiNoteForDrum;
		if (value == MIDI_NOTE_NONE) {
			value = kNoteForDrum;
		}
		this->setValue(value);
	}
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		int32_t value = this->getValue();
		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					soundDrum->outputMidiNoteForDrum = value;
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			soundEditor.currentSound->outputMidiNoteForDrum = value;
		}
	}
};
} // namespace deluge::gui::menu_item::midi::sound
