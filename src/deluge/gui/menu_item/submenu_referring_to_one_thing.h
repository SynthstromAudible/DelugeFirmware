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
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"
#include "submenu.h"

namespace deluge::gui::menu_item {
class SubmenuReferringToOneThing : public Submenu {
public:
	SubmenuReferringToOneThing(l10n::String newName, std::initializer_list<MenuItem*> newItems, int32_t newThingIndex)
	    : Submenu(newName, newItems), thingIndex(newThingIndex) {}

	SubmenuReferringToOneThing(l10n::String newName, std::span<MenuItem*> newItems, int32_t newThingIndex)
	    : Submenu(newName, newItems), thingIndex(newThingIndex) {}

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override {
		soundEditor.currentSourceIndex = thingIndex;
		soundEditor.currentSource = &soundEditor.currentSound->sources[thingIndex];
		soundEditor.currentSampleControls = &soundEditor.currentSource->sampleControls;
		Submenu::beginSession(navigatedBackwardFrom);
	}

	uint8_t thingIndex;
};
} // namespace deluge::gui::menu_item
