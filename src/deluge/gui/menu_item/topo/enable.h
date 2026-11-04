/*
 * Copyright © 2026 Synthstrom Audible Limited
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
#include "definitions_cxx.hpp"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::topo {

/// Turns the "Topo Drums" generative mode on / off for the current kit clip. When turning on,
/// makes sure the three engine banks have lanes (creating GateDrums if necessary).
class Enable final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		setValue(clip && clip->topoDrumsSettings.enabled ? 1 : 0);
	}

	void writeCurrentValue() override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (!clip) {
			return;
		}
		bool turnOn = getValue() != 0;
		if (turnOn && !clip->topoDrumsSettings.enabled) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			if (!clip->ensureTopoLanes(modelStack)) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_TOPO_NO_FREE_GATE_OUTPUTS));
			}
		}
		clip->topoDrumsSettings.enabled = turnOn;
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_OFF), //<
		    l10n::getView(STRING_FOR_ON),  //<
		};
	}

	bool isToggle() override { return true; }
	bool shouldEnterSubmenu() override { return false; }
};

} // namespace deluge::gui::menu_item::topo
