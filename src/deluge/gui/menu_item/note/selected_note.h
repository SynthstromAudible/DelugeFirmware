/*
 * Copyright (c) 2024 Sean Ditny
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
#include "gui/views/instrument_clip_view.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"

namespace deluge::gui::menu_item::note {
class SelectedNote : public Integer {
public:
	using Integer::Integer;

	bool shouldEnterSubmenu() override {
		int32_t x_display = instrumentClipView.lastSelectedNoteXDisplay;
		int32_t y_display = instrumentClipView.lastSelectedNoteYDisplay;
		if (x_display != kNoSelection && y_display != kNoSelection) {
			if (instrumentClipView.gridSquareInfo[y_display][x_display].isValid) {
				return true;
			}
		}
		display->displayPopup("Select Note");
		return false;
	}
};
} // namespace deluge::gui::menu_item::note
