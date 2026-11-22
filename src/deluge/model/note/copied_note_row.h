/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "model/note/note_locks.h"
#include <cstdint>
class Note;

class CopiedNoteRow {
public:
	CopiedNoteRow();
	virtual ~CopiedNoteRow();

	int16_t yDisplay;
	int32_t numNotes;
	Note* notes;
	CopiedNoteRow* next;
	int16_t yNote;
	/// The copied notes' parameter locks. The clipboard outlives songs, so the notes' lockSet indices point in here
	/// rather than into a Song's pool; pasting re-interns them into the destination song's pool.
	deluge::note_locks::Pool locks;
	int8_t accidentalOffset; // Semitones from the in-scale row this was displayed on (accidentals)
};
