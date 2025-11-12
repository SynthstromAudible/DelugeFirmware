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
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/sample/utils.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::sample {

class Repeat final : public Selection, public FormattedTitle {
public:
	Repeat(l10n::String name, l10n::String title_format_str, uint8_t source_id)
	    : Selection(name), FormattedTitle(title_format_str), source_id_{source_id} {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t) override {
		return isSampleModeSample(modControllable, source_id_);
	}

	bool usesAffectEntire() override { return true; }
	void readCurrentValue() override {
		const auto& source = soundEditor.currentSound->sources[source_id_];
		setValue(source.repeatMode);
	}
	void writeCurrentValue() override {
		const auto current_value = getValue<SampleRepeatMode>();

		Kit* kit = getCurrentKit();

		// If affect-entire button held, do whole kit
		if (kit != nullptr && currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR) {

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					Source* source = &soundDrum->sources[source_id_];

					// Automatically switch pitch/speed independence on / off if stretch-to-note-length mode is selected
					if (current_value == SampleRepeatMode::STRETCH) {
						soundDrum->killAllVoices();
						source->sampleControls.pitchAndSpeedAreIndependent = true;
					}
					else if (source->repeatMode == SampleRepeatMode::STRETCH) {
						soundDrum->killAllVoices();
						source->sampleControls.pitchAndSpeedAreIndependent = false;
					}

					if (current_value == SampleRepeatMode::ONCE) {
						// Send note-off for kit arpeggiator to avoid stuck notes
						sendNoteOffForKitArpeggiator(kit);
					}

					source->repeatMode = current_value;
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			Source& source = soundEditor.currentSound->sources[source_id_];

			// Automatically switch pitch/speed independence on / off if stretch-to-note-length mode is selected
			if (current_value == SampleRepeatMode::STRETCH) {
				soundEditor.currentSound->killAllVoices();
				source.sampleControls.pitchAndSpeedAreIndependent = true;
			}
			else if (source.repeatMode == SampleRepeatMode::STRETCH) {
				soundEditor.currentSound->killAllVoices();
				source.sampleControls.pitchAndSpeedAreIndependent = false;
			}

			if (kit != nullptr && current_value == SampleRepeatMode::ONCE) {
				// Send note-off for kit arpeggiator to avoid stuck notes
				sendNoteOffForKitArpeggiator(kit);
			}

			source.repeatMode = current_value;
		}

		// We need to re-render all rows, because this will have changed whether Note tails are displayed. Probably just
		// one row, but we don't know which
		uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
	}
	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		return {
		    l10n::getView(l10n::String::STRING_FOR_CUT),
		    l10n::getView(l10n::String::STRING_FOR_ONCE),
		    l10n::getView(l10n::String::STRING_FOR_LOOP),
		    l10n::getView(l10n::String::STRING_FOR_STRETCH),
		};
	}

	void renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) override {
		const auto& source = soundEditor.currentSound->sources[source_id_];
		const Icon& icon = [&] {
			switch (source.repeatMode) {
			case SampleRepeatMode::CUT:
				return OLED::sampleModeCutIcon;
			case SampleRepeatMode::ONCE:
				return OLED::sampleModeOnceIcon;
			case SampleRepeatMode::LOOP:
				return OLED::sampleModeLoopIcon;
			case SampleRepeatMode::STRETCH:
				return OLED::sampleModeStretchIcon;
			}
			return OLED::sampleModeCutIcon;
		}();
		OLED::main.drawIcon(icon, slot.start_x + 4, slot.start_y + kHorizontalMenuSlotYOffset - 4);
	}

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		Selection::configureRenderingOptions(options);
		options.label = getOptions(OptType::SHORT)[getValue()].data();
	}

private:
	uint8_t source_id_;

	static void sendNoteOffForKitArpeggiator(Kit* kit) {
		int32_t noteRowIndex;
		NoteRow* noteRow = getCurrentInstrumentClip()->getNoteRowForDrum(kit->selectedDrum, &noteRowIndex);
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = (ModelStack*)modelStackMemory;
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addTimelineCounter(getCurrentClip())
		        ->addNoteRow(noteRowIndex, noteRow)
		        ->addOtherTwoThings(soundEditor.currentModControllable, soundEditor.currentParamManager);
		kit->noteOffPreKitArp(modelStackWithThreeMainThings, kit->selectedDrum);
	}
};

} // namespace deluge::gui::menu_item::sample
