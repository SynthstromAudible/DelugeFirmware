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
#include "storage/multi_range/multisample_range.h"
#include "gui/menu_item/source/transpose.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::sample {
class Transpose final : public source::Transpose, public FormattedTitle {
public:
	Transpose(const string& name, const string& title_format_str, int newP)
	    : source::Transpose(name, newP), FormattedTitle(title_format_str) {}

	[[nodiscard]] const string& getTitle() const override { return FormattedTitle::title(); }

	void readCurrentValue() override {
		int transpose = 0;
		int cents = 0;
		if ((soundEditor.currentMultiRange != nullptr) && soundEditor.currentSound->getSynthMode() != SynthMode::FM
		    && soundEditor.currentSource->oscType == OscType::SAMPLE) {
			transpose = (dynamic_cast<MultisampleRange*>(soundEditor.currentMultiRange))->sampleHolder.transpose;
			cents = (dynamic_cast<MultisampleRange*>(soundEditor.currentMultiRange))->sampleHolder.cents;
		}
		else {
			transpose = soundEditor.currentSource->transpose;
			cents = soundEditor.currentSource->cents;
		}
		this->value_ = transpose * 100 + cents;
	}

	void writeCurrentValue() override {
		int currentValue = this->value_ + 25600;

		int semitones = (currentValue + 50) / 100;
		int cents = currentValue - semitones * 100;

		int transpose = semitones - 256;
		if ((soundEditor.currentMultiRange != nullptr) && soundEditor.currentSound->getSynthMode() != SynthMode::FM
		    && soundEditor.currentSource->oscType == OscType::SAMPLE) {
			(dynamic_cast<MultisampleRange*>(soundEditor.currentMultiRange))->sampleHolder.transpose = transpose;
			(dynamic_cast<MultisampleRange*>(soundEditor.currentMultiRange))->sampleHolder.setCents(cents);
		}
		else {
			soundEditor.currentSource->transpose = transpose;
			soundEditor.currentSource->setCents(cents);
		}

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();

		soundEditor.currentSound->recalculateAllVoicePhaseIncrements(modelStack);
	}

	MenuPermission checkPermissionToBeginSession(Sound* sound, int whichThing, ::MultiRange** currentRange) override {

		if (!isRelevant(sound, whichThing)) {
			return MenuPermission::NO;
		}

		Source* source = &sound->sources[whichThing];

		if (sound->getSynthMode() == SynthMode::FM
		    || (source->oscType != OscType::SAMPLE && source->oscType != OscType::WAVETABLE)) {
			return MenuPermission::YES;
		}

		return soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, whichThing, true, currentRange);
	}

	bool isRangeDependent() override { return true; }
};
} // namespace deluge::gui::menu_item::sample
