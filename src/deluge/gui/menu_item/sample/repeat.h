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
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::sample {

class Repeat final : public Selection, public FormattedTitle {
public:
	Repeat(l10n::String name, l10n::String title_format_str) : Selection(name), FormattedTitle(title_format_str) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) final {
		return isSampleModeSample(modControllable, whichThing);
	}

	bool usesAffectEntire() override { return true; }
	void readCurrentValue() override { this->setValue(soundEditor.currentSource->repeatMode); }
	void writeCurrentValue() override {
		auto current_value = this->getValue<SampleRepeatMode>();

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					Source* source = &soundDrum->sources[soundEditor.currentSourceIndex];

					// Automatically switch pitch/speed independence on / off if stretch-to-note-length mode is selected
					if (current_value == SampleRepeatMode::STRETCH) {
						soundDrum->unassignAllVoices();
						source->sampleControls.pitchAndSpeedAreIndependent = true;
					}
					else if (source->repeatMode == SampleRepeatMode::STRETCH) {
						soundDrum->unassignAllVoices();
						soundEditor.currentSource->sampleControls.pitchAndSpeedAreIndependent = false;
					}

					source->repeatMode = current_value;
				}
			}
		}

		// Or, the normal case of just one sound
		else {
			// Automatically switch pitch/speed independence on / off if stretch-to-note-length mode is selected
			if (static_cast<SampleRepeatMode>(current_value) == SampleRepeatMode::STRETCH) {
				soundEditor.currentSound->unassignAllVoices();
				soundEditor.currentSource->sampleControls.pitchAndSpeedAreIndependent = true;
			}
			else if (soundEditor.currentSource->repeatMode == SampleRepeatMode::STRETCH) {
				soundEditor.currentSound->unassignAllVoices();
				soundEditor.currentSource->sampleControls.pitchAndSpeedAreIndependent = false;
			}

			soundEditor.currentSource->repeatMode = current_value;
		}

		// We need to re-render all rows, because this will have changed whether Note tails are displayed. Probably just
		// one row, but we don't know which
		uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
	}
	deluge::vector<std::string_view> getOptions() override {
		return {
		    l10n::getView(l10n::String::STRING_FOR_CUT),
		    l10n::getView(l10n::String::STRING_FOR_ONCE),
		    l10n::getView(l10n::String::STRING_FOR_LOOP),
		    l10n::getView(l10n::String::STRING_FOR_STRETCH),
		};
	}
};

} // namespace deluge::gui::menu_item::sample
