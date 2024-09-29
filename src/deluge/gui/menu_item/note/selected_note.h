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
#include "definitions_cxx.hpp"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::note {
class SelectedNote : public Integer {
public:
	using Integer::Integer;

	bool shouldEnterSubmenu() {
		int32_t xDisplay = instrumentClipView.lastSelectedNoteXDisplay;
		int32_t yDisplay = instrumentClipView.lastSelectedNoteYDisplay;
		if (xDisplay != kNoSelection && yDisplay != kNoSelection) {
			if (instrumentClipView.gridSquareInfo[yDisplay][xDisplay].isValid) {
				return true;
			}
		}
		display->displayPopup("Select Note");
		return false;
	}
};
} // namespace deluge::gui::menu_item::note
