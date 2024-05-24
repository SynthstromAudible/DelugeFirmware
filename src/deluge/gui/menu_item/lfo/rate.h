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
#include "gui/menu_item/patched_param/integer.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::lfo {
class Rate final : public patched_param::Integer {
public:
	Rate(deluge::l10n::String name, deluge::l10n::String type, int32_t newP, uint8_t lfoId)
	    : Integer(name, type, newP), lfoId_(lfoId) {}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		Sound* sound = static_cast<Sound*>(modControllable);
		return sound->lfoConfig[lfoId_].syncLevel == SYNC_LEVEL_NONE;
	}

private:
	uint8_t lfoId_;
};
} // namespace deluge::gui::menu_item::lfo
