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
#include "gui/menu_item/decimal.h"
#include "gui/menu_item/menu_item_with_cc_learning.h"
#include "model/clip/audio_clip.h"
#include "gui/ui/sound_editor.h"
#include "model/song/song.h"

namespace menu_item::audio_clip {
class Transpose final : public Decimal, public MenuItemWithCCLearning {
public:
	using Decimal::Decimal;
	void readCurrentValue() {
		soundEditor.currentValue = ((AudioClip*)currentSong->currentClip)->sampleHolder.transpose * 100
		                           + ((AudioClip*)currentSong->currentClip)->sampleHolder.cents;
	}
	void writeCurrentValue() {
		int currentValue = soundEditor.currentValue + 25600;

		int semitones = (currentValue + 50) / 100;
		int cents = currentValue - semitones * 100;
		int transpose = semitones - 256;

		((AudioClip*)currentSong->currentClip)->sampleHolder.transpose = transpose;
		((AudioClip*)currentSong->currentClip)->sampleHolder.cents = cents;

		((AudioClip*)currentSong->currentClip)->sampleHolder.recalculateNeutralPhaseIncrement();
	}

	int getMinValue() const { return -9600; }
	int getMaxValue() const { return 9600; }
	int getNumDecimalPlaces() const { return 2; }

	void unlearnAction() { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};

	ParamDescriptor getLearningThing() {
		ParamDescriptor paramDescriptor;
		paramDescriptor.setToHaveParamOnly(PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_GLOBALEFFECTABLE_PITCH_ADJUST);
		return paramDescriptor;
	}
};
} // namespace menu_item::audio_clip
