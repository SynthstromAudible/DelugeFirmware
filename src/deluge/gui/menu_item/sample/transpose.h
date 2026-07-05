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
#include "gui/ui/sound_editor.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "storage/multi_range/multisample_range.h"

// NOTE: This is actually the Oscillator transpose!

namespace deluge::gui::menu_item::sample {
class Transpose final : public source::Transpose, public FormattedTitle {
public:
	Transpose(l10n::String name, l10n::String title_format_str, int32_t newP, uint8_t source_id)
	    : source::Transpose(name, newP, source_id), FormattedTitle(title_format_str, source_id + 1) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	void readCurrentValue() override {
		int32_t transpose = 0;
		int32_t cents = 0;

		Source& source = soundEditor.currentSound->sources[source_id_];

		if (!source.ranges.empty() && soundEditor.currentSound->getSynthMode() != SynthMode::FM
		    && source.oscType == OscType::SAMPLE) {
			const auto* multi_sample_range =
			    soundEditor.currentSourceIndex == source_id_ && soundEditor.currentMultiRange != nullptr
			        ? static_cast<MultisampleRange*>(soundEditor.currentMultiRange)
			        : static_cast<MultisampleRange*>(source.ranges.getElement(0));
			transpose = multi_sample_range->sampleHolder.transpose;
			cents = multi_sample_range->sampleHolder.cents;
		}
		else {
			transpose = source.transpose;
			cents = source.cents;
		}
		this->setValue(computeCurrentValueForTranspose(transpose, cents));
	}

	bool usesAffectEntire() override { return true; }

	void writeCurrentValue() override {
		int32_t transpose, cents;
		computeFinalValuesForTranspose(this->getValue(), &transpose, &cents);

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					Source& source = soundDrum->sources[source_id_];

					if (!source.ranges.empty() && soundDrum->getSynthMode() != SynthMode::FM
					    && source.oscType == OscType::SAMPLE) {
						auto* multi_sample_range = static_cast<MultisampleRange*>(source.ranges.getElement(0));
						multi_sample_range->sampleHolder.transpose = transpose;
						multi_sample_range->sampleHolder.setCents(cents);
					}
					else {
						source.transpose = transpose;
						source.setCents(cents);
					}

					char modelStackMemoryForSoundDrum[MODEL_STACK_MAX_SIZE];
					ModelStackWithSoundFlags* modelStackForSoundDrum =
					    getModelStackFromSoundDrum(modelStackMemoryForSoundDrum, soundDrum)->addSoundFlags();

					soundDrum->recalculateAllVoicePhaseIncrements(modelStackForSoundDrum);
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			Source& source = soundEditor.currentSound->sources[source_id_];

			if (!source.ranges.empty() && soundEditor.currentSound->getSynthMode() != SynthMode::FM
			    && source.oscType == OscType::SAMPLE) {
				auto* multi_sample_range =
				    soundEditor.currentSourceIndex == source_id_ && soundEditor.currentMultiRange != nullptr
				        ? static_cast<MultisampleRange*>(soundEditor.currentMultiRange)
				        : static_cast<MultisampleRange*>(source.ranges.getElement(0));
				multi_sample_range->sampleHolder.transpose = transpose;
				multi_sample_range->sampleHolder.setCents(cents);
			}
			else {
				source.transpose = transpose;
				source.setCents(cents);
			}

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();

			soundEditor.currentSound->recalculateAllVoicePhaseIncrements(modelStack);
		}
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t,
	                                             ::MultiRange** currentRange) override {
		if (!isRelevant(modControllable, source_id_)) {
			return MenuPermission::NO;
		}

		const auto sound = static_cast<Sound*>(modControllable);
		const Source& source = sound->sources[source_id_];

		if (sound->getSynthMode() == SynthMode::FM
		    || (source.oscType != OscType::SAMPLE && source.oscType != OscType::WAVETABLE)) {
			return MenuPermission::YES;
		}

		return soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, source_id_, currentRange);
	}

	bool isRangeDependent() override { return true; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t) override {
		const auto sound = static_cast<Sound*>(modControllable);
		if (Source& source = sound->sources[source_id_];
		    source.oscType == OscType::SAMPLE || source.oscType == OscType::WAVETABLE) {
			return source.hasAtLeastOneAudioFileLoaded();
		}
		return true;
	}

	void getNotificationValue(etl::istring& valueBuf) override {
		Decimal::getNotificationValue(valueBuf);

		Source& source = soundEditor.currentSound->sources[source_id_];
		const int32_t rangeIndex = soundEditor.currentMultiRangeIndex;
		const int32_t numRanges = source.ranges.size();

		if (soundEditor.currentSourceIndex != source_id_ || soundEditor.currentMultiRange == nullptr || numRanges <= 1
		    || rangeIndex < 0 || rangeIndex >= numRanges || source.oscType != OscType::SAMPLE) {
			return;
		}

		valueBuf.append(" (");

		if (rangeIndex == 0) {
			valueBuf.append(l10n::get(l10n::String::STRING_FOR_BOTTOM));
		}
		else {
			char noteName[8];
			noteCodeToString(source.ranges.getElement(rangeIndex - 1)->topNote + 1, noteName);
			valueBuf.append(noteName);
		}

		valueBuf.append("-");

		if (rangeIndex == numRanges - 1) {
			valueBuf.append("top");
		}
		else {
			char noteName[8];
			noteCodeToString(source.ranges.getElement(rangeIndex)->topNote, noteName);
			valueBuf.append(noteName);
		}

		valueBuf.append(")");
	}
};
} // namespace deluge::gui::menu_item::sample
