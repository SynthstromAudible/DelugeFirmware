/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "model/note/note_row.h"
#include "util/container/ordered_pos_vector.h"

class NoteRowVector final : public deluge::OrderedPosVector<NoteRow, &NoteRow::y> {
public:
	NoteRow* insertNoteRowAtIndex(int32_t index);
	NoteRow* insertNoteRowAtY(int32_t y, int32_t* getIndex = nullptr);
	void deleteNoteRowAtIndex(int32_t index, int32_t numToDelete = 1);
	// Deep copies go through copy-assignment (`dst = src`), inherited from OrderedPosVector — each NoteRow is
	// copy-constructed (its param manager + notes deep-copied). Throws deluge::exception on OOM.
};
