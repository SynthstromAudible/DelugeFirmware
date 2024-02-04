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
#include "gui/menu_item/ppqn.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"

// Trigger clock in menu
namespace deluge::gui::menu_item::trigger::in {
class PPQN : public menu_item::PPQN {
public:
	using menu_item::PPQN::PPQN;
	void readCurrentValue() override { this->setValue(playbackHandler.analogInTicksPPQN); }
	void writeCurrentValue() override {
		playbackHandler.analogInTicksPPQN = this->getValue();
		if ((playbackHandler.playbackState & PLAYBACK_CLOCK_EXTERNAL_ACTIVE) && playbackHandler.usingAnalogClockInput)
			playbackHandler.resyncInternalTicksToInputTicks(currentSong);
	}
};
} // namespace deluge::gui::menu_item::trigger::in
