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
#include "model/clip/sequencer/modes/lanes_sequencer_mode.h"

namespace deluge::gui::menu_item::lanes {

using deluge::model::clip::sequencer::modes::getCurrentLanesEngine;

/// Track length: all lanes restart after this many ticks. 0 = off (free-running).
class TrackLength final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMaxValue() const override { return 256; }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }

	void readCurrentValue() override {
		LanesEngine* engine = getCurrentLanesEngine();
		if (engine) {
			this->setValue(engine->trackLength);
		}
	}

	void writeCurrentValue() override {
		LanesEngine* engine = getCurrentLanesEngine();
		if (engine) {
			engine->trackLength = static_cast<uint16_t>(this->getValue());
			engine->globalTickCounter = 0;
		}
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {
		return MenuPermission::YES;
	}
};

} // namespace deluge::gui::menu_item::lanes
