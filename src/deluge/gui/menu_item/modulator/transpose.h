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
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::modulator {

class Transpose final : public source::Transpose, public FormattedTitle {
public:
	Transpose(const string& name, const fmt::format_string<int32_t>& title_format_str, int32_t newP)
	    : source::Transpose(name, newP), FormattedTitle(title_format_str) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	void readCurrentValue() override {
		this->set_value((int32_t)soundEditor.currentSound->modulatorTranspose[soundEditor.currentSourceIndex] * 100
		                + soundEditor.currentSound->modulatorCents[soundEditor.currentSourceIndex]);
	}

	void writeCurrentValue() override {
		int32_t currentValue = this->get_value() + 25600;

		int32_t semitones = (currentValue + 50) / 100;
		int32_t cents = currentValue - semitones * 100;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();

		soundEditor.currentSound->setModulatorTranspose(soundEditor.currentSourceIndex, semitones - 256, modelStack);
		soundEditor.currentSound->setModulatorCents(soundEditor.currentSourceIndex, cents, modelStack);
	}

	bool isRelevant(Sound* sound, int32_t whichThing) override { return (sound->getSynthMode() == SynthMode::FM); }
};

} // namespace deluge::gui::menu_item::modulator
