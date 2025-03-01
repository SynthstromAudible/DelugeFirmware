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

namespace deluge::gui::menu_item::unison {
class Detune final : public Integer {
public:
	using Integer::Integer;

	ModelStackWithThreeMainThings* getModelStackFromSoundDrum(void* memory, SoundDrum* soundDrum) {
		InstrumentClip* clip = getCurrentInstrumentClip();
		int32_t noteRowIndex;
		NoteRow* noteRow = clip->getNoteRowForDrum(soundDrum, &noteRowIndex);
		return setupModelStackWithThreeMainThingsIncludingNoteRow(memory, currentSong, getCurrentClip(), noteRowIndex,
		                                                          noteRow, soundDrum, &noteRow->paramManager);
	}
	void readCurrentValue() override { this->setValue(soundEditor.currentSound->unisonDetune); }
	void writeCurrentValue() override {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();

		soundEditor.currentSound->setUnisonDetune(this->getValue(), modelStack);
	}
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxUnisonDetune; }
};
} // namespace deluge::gui::menu_item::unison
