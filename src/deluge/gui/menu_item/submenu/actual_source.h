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
#include "gui/menu_item/submenu_referring_to_one_thing.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"
#include "string.h"
#include "util/cfunctions.h"

extern void setOscillatorNumberForTitles(int32_t);

namespace deluge::gui::menu_item::submenu {
class ActualSource final : public SubmenuReferringToOneThing {
public:
	using SubmenuReferringToOneThing::SubmenuReferringToOneThing;

	// OLED Only
	void beginSession(MenuItem* navigatedBackwardFrom) {
		setOscillatorNumberForTitles(this->thingIndex);
		SubmenuReferringToOneThing::beginSession(navigatedBackwardFrom);
	}

	// 7seg Only
	void drawName() override {
		if (soundEditor.currentSound->getSynthMode() == SynthMode::FM) {
			char buffer[5];
			strcpy(buffer, "CAR");
			intToString(this->thingIndex + 1, buffer + 3);
			display->setText(buffer);
		}
		else {
			SubmenuReferringToOneThing::drawName();
		}
	}
};

} // namespace deluge::gui::menu_item::submenu
