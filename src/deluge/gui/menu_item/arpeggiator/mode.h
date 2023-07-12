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
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "gui/menu_item/selection.h"
#include "model/song/song.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::arpeggiator {
class Mode final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->value_ = soundEditor.currentArpSettings->mode; }
	void writeCurrentValue() override {

		// If was off, or is now becoming off...
		if (soundEditor.currentArpSettings->mode == ARP_MODE_OFF || this->value_ == ARP_MODE_OFF) {
			if (currentSong->currentClip->isActiveOnOutput()) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);

				if (soundEditor.editingCVOrMIDIClip()) {
					(dynamic_cast<InstrumentClip*>(currentSong->currentClip))
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
		soundEditor.currentArpSettings->mode = this->value_;

		// Only update the Clip-level arp setting if they hadn't been playing with other synth parameters first (so it's clear that switching the arp on or off was their main intention)
		if (!soundEditor.editingKit()) {
			bool arpNow = (this->value_ != ARP_MODE_OFF); // Uh.... this does nothing...
		}
	}
	Sized<char const**> getOptions() override {
		static char const* options[] = {"OFF", "UP", "DOWN", "BOTH", "Random"};
		return {options, NUM_ARP_MODES};
	}
};
} // namespace deluge::gui::menu_item::arpeggiator
