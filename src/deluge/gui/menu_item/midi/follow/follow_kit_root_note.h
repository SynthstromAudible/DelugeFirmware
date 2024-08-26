/*
 * Copyright (c) 2024 Sean Ditny
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
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "io/midi/midi_engine.h"

namespace deluge::gui::menu_item::midi {

class FollowKitRootNote final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->setValue(midiEngine.midiFollowKitRootNote); }
	void writeCurrentValue() override { midiEngine.midiFollowKitRootNote = this->getValue(); }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMIDIValue; }
	bool allowsLearnMode() override { return true; }

	bool learnNoteOn(MIDIDevice* device, int32_t channel, int32_t noteCode) {
		this->setValue(noteCode);
		midiEngine.midiFollowKitRootNote = noteCode;

		if (soundEditor.getCurrentMenuItem() == this) {
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			else {
				drawValue();
			}
		}
		else {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_LEARNED));
		}

		return true;
	}
};
} // namespace deluge::gui::menu_item::midi
