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
#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "gui/menu_item/integer.h"
#include "processing/sound/sound_drum.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/kit.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::sample {
class TimeStretch final : public Integer, public FormattedTitle {
public:
	TimeStretch(const string& name, const string& title_format_str) : Integer(name), FormattedTitle(title_format_str) {}

	[[nodiscard]] const string& getTitle() const override { return FormattedTitle::title(); }

	bool usesAffectEntire() override { return true; }

	void readCurrentValue() override { this->value_ = soundEditor.currentSource->timeStretchAmount; }

	void writeCurrentValue() override {

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {

			Kit* kit = dynamic_cast<Kit*>(currentSong->currentClip->output);

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DRUM_TYPE_SOUND) {
					auto* soundDrum = dynamic_cast<SoundDrum*>(thisDrum);
					Source* source = &soundDrum->sources[soundEditor.currentSourceIndex];

					source->timeStretchAmount = this->value_;
				}
			}
		}

		// Or, the normal case of just one sound
		else {
			soundEditor.currentSource->timeStretchAmount = this->value_;
		}
	}
	[[nodiscard]] int getMinValue() const override { return -48; }
	[[nodiscard]] int getMaxValue() const override { return 48; }
	bool isRelevant(Sound* sound, int whichThing) override {
		Source* source = &sound->sources[whichThing];
		return (sound->getSynthMode() == SYNTH_MODE_SUBTRACTIVE && source->oscType == OSC_TYPE_SAMPLE);
	}
};
} // namespace deluge::gui::menu_item::sample
