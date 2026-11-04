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
#include "model/clip/instrument_clip.h"
#include "util/d_stringbuf.h"

namespace deluge::gui::menu_item::topo {

/// One-shot action: writes the engine's next-loop pattern onto the lane rows as notes, then
/// switches Topo Drums off. The selection springs back to "Off" after firing.
class Capture final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { setValue(0); }

	void writeCurrentValue() override {
		if (getValue() == 0) {
			return;
		}
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (clip) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			int32_t notesWritten = clip->captureTopoPattern(modelStack);

			DEF_STACK_STRING_BUF(text, 30);
			text.append(l10n::get(l10n::String::STRING_FOR_TOPO_CAPTURED));
			text.append(' ');
			text.appendInt(notesWritten);
			display->displayPopup(text.c_str());
		}
		setValue(0);
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_OFF),          //<
		    l10n::getView(STRING_FOR_TOPO_CAPTURE), //<
		};
	}
};

} // namespace deluge::gui::menu_item::topo
