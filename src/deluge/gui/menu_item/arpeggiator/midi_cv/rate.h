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
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "model/clip/instrument_clip.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::arpeggiator::midi_cv {
class Rate final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override {
		this->set_value(
		    (((int64_t)(static_cast<InstrumentClip*>(currentSong->currentClip))->arpeggiatorRate + 2147483648) * 50
		     + 2147483648)
		    >> 32);
	}
	void writeCurrentValue() override {
		if (this->get_value() == 25) {
			(static_cast<InstrumentClip*>(currentSong->currentClip))->arpeggiatorRate = 0;
		}
		else {
			(static_cast<InstrumentClip*>(currentSong->currentClip))->arpeggiatorRate =
			    (uint32_t)this->get_value() * 85899345 - 2147483648;
		}
	}
	[[nodiscard]] int32_t getMaxValue() const override { return 50; }
	bool isRelevant(Sound* sound, int32_t whichThing) override { return soundEditor.editingCVOrMIDIClip(); }
};
} // namespace deluge::gui::menu_item::arpeggiator::midi_cv
