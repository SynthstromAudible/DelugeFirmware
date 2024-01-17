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
#include "gui/ui/sound_editor.h"
#include "model/clip/audio_clip.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::audio_clip {
class Transpose final : public Decimal, public MenuItemWithCCLearning {
public:
	using Decimal::Decimal;
	void readCurrentValue() override {
		this->setValue(getCurrentAudioClip()->sampleHolder.transpose * 100 + getCurrentAudioClip()->sampleHolder.cents);
	}
	void writeCurrentValue() override {
		int32_t currentValue = this->getValue() + 25600;

		int32_t semitones = (currentValue + 50) / 100;
		int32_t cents = currentValue - semitones * 100;
		int32_t transpose = semitones - 256;

		auto& sampleHolder = getCurrentAudioClip()->sampleHolder;
		sampleHolder.transpose = transpose;
		sampleHolder.cents = cents;
		sampleHolder.recalculateNeutralPhaseIncrement();
	}

	[[nodiscard]] int32_t getMinValue() const override { return -9600; }
	[[nodiscard]] int32_t getMaxValue() const override { return 9600; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 2; }

	void unlearnAction() override { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() override { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDIDevice* fromDevice, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) override {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};

	ParamDescriptor getLearningThing() override {
		ParamDescriptor paramDescriptor;
		paramDescriptor.setToHaveParamOnly(
		    deluge::modulation::params::Param::Unpatched::START
		    + deluge::modulation::params::Param::Unpatched::GlobalEffectable::PITCH_ADJUST);
		return paramDescriptor;
	}
};
} // namespace deluge::gui::menu_item::audio_clip
