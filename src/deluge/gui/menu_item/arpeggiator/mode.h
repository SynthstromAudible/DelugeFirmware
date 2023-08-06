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
#include "gui/l10n/l10n.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "util/misc.h"

namespace deluge::gui::menu_item::arpeggiator {
class Mode final : public Selection<kNumArpModes> {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(soundEditor.currentArpSettings->mode); }
	void writeCurrentValue() override {
		auto current_value = this->getValue<ArpMode>();

		// If was off, or is now becoming off...
		if (soundEditor.currentArpSettings->mode == ArpMode::OFF || current_value == ArpMode::OFF) {
			if (currentSong->currentClip->isActiveOnOutput()) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);

				if (soundEditor.editingCVOrMIDIClip()) {
					(static_cast<InstrumentClip*>(currentSong->currentClip))
					    ->stopAllNotesForMIDIOrCV(modelStack->toWithTimelineCounter());
				}
				else {
					ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();
					soundEditor.currentSound->allNotesOff(
					    modelStackWithSoundFlags,
					    soundEditor.currentSound->getArp()); // Must switch off all notes when switching arp on / off
					soundEditor.currentSound->reassessRenderSkippingStatus(modelStackWithSoundFlags);
				}
			}
		}
		soundEditor.currentArpSettings->mode = current_value;

		// Only update the Clip-level arp setting if they hadn't been playing with other synth parameters first (so it's clear that switching the arp on or off was their main intention)
		if (!soundEditor.editingKit()) {
			bool arpNow = (current_value != ArpMode::OFF); // Uh.... this does nothing...
		}
	}
	static_vector<std::string, capacity()> getOptions() override {
		using enum l10n::Strings;
		return {
		    l10n::get(STRING_FOR_DISABLED), //<
		    l10n::get(STRING_FOR_UP),       //<
		    l10n::get(STRING_FOR_DOWN),     //<
		    l10n::get(STRING_FOR_BOTH),     //<
		    l10n::get(STRING_FOR_RANDOM),   //<
		};
	}
};
} // namespace deluge::gui::menu_item::arpeggiator
