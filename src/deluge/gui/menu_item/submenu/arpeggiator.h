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
#include "gui/menu_item/submenu.h"
#include "gui/ui/sound_editor.h"
#include "model/clip/instrument_clip.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::submenu {

template <size_t n>
class Arpeggiator final : public Submenu<n> {
public:
	using Submenu<n>::Submenu;
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override {

		soundEditor.currentArpSettings = soundEditor.editingKit()
		                                     ? &(dynamic_cast<SoundDrum*>(soundEditor.currentSound))->arpSettings
		                                     : &(dynamic_cast<InstrumentClip*>(currentSong->currentClip))->arpSettings;
		Submenu<n>::beginSession(navigatedBackwardFrom);
	}
};

// Template deduction guide, will not be required with P2582@C++23
template <size_t n>
Arpeggiator(char const*, MenuItem* const (&)[n]) -> Arpeggiator<n>;
} // namespace deluge::gui::menu_item::submenu
