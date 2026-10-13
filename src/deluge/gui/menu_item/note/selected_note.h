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
		// check if we've selected a note, either by holding note(s) or by having a remembered note
		if (instrumentClipView.getLeftMostNotePressed()) {
			return true;
		}
		// no note is selected, shouldn't be possible but if it happens show pop-up
		display->displayPopup("Select Note");
		return false;
	}

	// only allow select encoder action if you've selected a note to edit
	bool selectEncoderActionIsPermitted() override { return shouldEnterSubmenu(); }
};
} // namespace deluge::gui::menu_item::note
