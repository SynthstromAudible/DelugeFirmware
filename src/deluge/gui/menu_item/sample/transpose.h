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
#include "storage/multi_range/multisample_range.h"

namespace deluge::gui::menu_item::sample {
class Transpose final : public source::Transpose, public FormattedTitle {
public:
	Transpose(l10n::String name, l10n::String title_format_str, int32_t newP)
	    : source::Transpose(name, newP), FormattedTitle(title_format_str) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	void readCurrentValue() override {
		int32_t transpose = 0;
		int32_t cents = 0;
		if ((soundEditor.currentMultiRange != nullptr) && soundEditor.currentSound->getSynthMode() != SynthMode::FM
		    && soundEditor.currentSource->oscType == OscType::SAMPLE) {
			transpose = (static_cast<MultisampleRange*>(soundEditor.currentMultiRange))->sampleHolder.transpose;
			cents = (static_cast<MultisampleRange*>(soundEditor.currentMultiRange))->sampleHolder.cents;
		}
		else {
			transpose = soundEditor.currentSource->transpose;
			cents = soundEditor.currentSource->cents;
		}
		this->setValue(computeCurrentValueForTranspose(transpose, cents));
	}

	void writeCurrentValue() override {
		int32_t transpose, cents;
		computeFinalValuesForTranspose(this->getValue(), &transpose, &cents);

		if ((soundEditor.currentMultiRange != nullptr) && soundEditor.currentSound->getSynthMode() != SynthMode::FM
		    && soundEditor.currentSource->oscType == OscType::SAMPLE) {
			(static_cast<MultisampleRange*>(soundEditor.currentMultiRange))->sampleHolder.transpose = transpose;
			(static_cast<MultisampleRange*>(soundEditor.currentMultiRange))->sampleHolder.setCents(cents);
		}
		else {
			soundEditor.currentSource->transpose = transpose;
			soundEditor.currentSource->setCents(cents);
		}

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();

		soundEditor.currentSound->recalculateAllVoicePhaseIncrements(modelStack);
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {

		if (!isRelevant(modControllable, whichThing)) {
			return MenuPermission::NO;
		}

		Sound* sound = static_cast<Sound*>(modControllable);
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
