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
#include "definitions.h"
#include "gui/menu_item/selection.h"
#include "model/clip/clip.h"
#include "gui/views/instrument_clip_view.h"
#include "model/drum/drum.h"
#include "model/drum/kit.h"
#include "selection.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "gui/ui/sound_editor.h"

namespace deluge::gui::menu_item::sample {

class Repeat final : public Selection {
public:
	using Selection::Selection;
	bool usesAffectEntire() override { return true; }
	void readCurrentValue() override { this->value_ = soundEditor.currentSource->repeatMode; }
	void writeCurrentValue() override {

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {

			Kit* kit = dynamic_cast<Kit*>(currentSong->currentClip->output);

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DRUM_TYPE_SOUND) {
					auto* soundDrum = dynamic_cast<SoundDrum*>(thisDrum);
					Source* source = &soundDrum->sources[soundEditor.currentSourceIndex];

					// Automatically switch pitch/speed independence on / off if stretch-to-note-length mode is selected
					if (this->value_ == SAMPLE_REPEAT_STRETCH) {
						soundDrum->unassignAllVoices();
						source->sampleControls.pitchAndSpeedAreIndependent = true;
					}
					else if (source->repeatMode == SAMPLE_REPEAT_STRETCH) {
						soundDrum->unassignAllVoices();
						soundEditor.currentSource->sampleControls.pitchAndSpeedAreIndependent = false;
					}

					source->repeatMode = this->value_;
				}
			}
		}

		// Or, the normal case of just one sound
		else {
			// Automatically switch pitch/speed independence on / off if stretch-to-note-length mode is selected
			if (this->value_ == SAMPLE_REPEAT_STRETCH) {
				soundEditor.currentSound->unassignAllVoices();
				soundEditor.currentSource->sampleControls.pitchAndSpeedAreIndependent = true;
			}
			else if (soundEditor.currentSource->repeatMode == SAMPLE_REPEAT_STRETCH) {
				soundEditor.currentSound->unassignAllVoices();
				soundEditor.currentSource->sampleControls.pitchAndSpeedAreIndependent = false;
			}

			soundEditor.currentSource->repeatMode = this->value_;
		}

		// We need to re-render all rows, because this will have changed whether Note tails are displayed. Probably just one row, but we don't know which
		uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
	}
	Sized<char const**> getOptions() override {
		static char const* options[] = {"CUT", "ONCE", "LOOP", "STRETCH"};
		return {options, NUM_REPEAT_MODES};
	}
};

} // namespace deluge::gui::menu_item::sample
