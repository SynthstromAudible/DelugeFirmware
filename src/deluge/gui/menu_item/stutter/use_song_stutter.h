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
#include "gui/menu_item/toggle.h"
#include "gui/ui/sound_editor.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::stutter {

class UseSongStutter final : public Toggle {
public:
	using Toggle::Toggle;
	void readCurrentValue() override {
		this->setValue(soundEditor.currentModControllable->stutterConfig.useSongStutter);
	}
	void writeCurrentValue() override {
		bool value = this->getValue();
		if (!value) {
			// Copy current song settings to this clip settings
			soundEditor.currentModControllable->stutterConfig.quantized =
			    currentSong->globalEffectable.stutterConfig.quantized;
			soundEditor.currentModControllable->stutterConfig.reversed =
			    currentSong->globalEffectable.stutterConfig.reversed;
			soundEditor.currentModControllable->stutterConfig.pingPong =
			    currentSong->globalEffectable.stutterConfig.pingPong;
		}
		soundEditor.currentModControllable->stutterConfig.useSongStutter = value;
	}
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return !soundEditor.currentModControllable->isSong();
	}
};

} // namespace deluge::gui::menu_item::stutter
