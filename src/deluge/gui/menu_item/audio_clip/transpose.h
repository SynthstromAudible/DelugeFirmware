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

namespace deluge::gui::menu_item::audio_clip {
class Transpose final : public Decimal, public MenuItemWithCCLearning {
public:
	using Decimal::Decimal;
	void readCurrentValue() override {
		this->value_ = (dynamic_cast<AudioClip*>(currentSong->currentClip))->sampleHolder.transpose * 100
		               + (dynamic_cast<AudioClip*>(currentSong->currentClip))->sampleHolder.cents;
	}
	void writeCurrentValue() override {
		int currentValue = this->value_ + 25600;

		int semitones = (currentValue + 50) / 100;
		int cents = currentValue - semitones * 100;
		int transpose = semitones - 256;

		auto& sampleHolder = (dynamic_cast<AudioClip*>(currentSong->currentClip))->sampleHolder;
		sampleHolder.transpose = transpose;
		sampleHolder.cents = cents;
		sampleHolder.recalculateNeutralPhaseIncrement();
	}

	[[nodiscard]] int getMinValue() const override { return -9600; }
	[[nodiscard]] int getMaxValue() const override { return 9600; }
	[[nodiscard]] int getNumDecimalPlaces() const override { return 2; }

	void unlearnAction() override { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() override { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) override {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};

	ParamDescriptor getLearningThing() override {
		ParamDescriptor paramDescriptor;
		paramDescriptor.setToHaveParamOnly(PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_GLOBALEFFECTABLE_PITCH_ADJUST);
		return paramDescriptor;
	}
};
} // namespace deluge::gui::menu_item::audio_clip
