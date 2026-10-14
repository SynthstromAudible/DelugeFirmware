/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "deluge-project/doc_note_row.h"
#include "deluge-project/launch_style.h"
#include <cstdint>
#include <list>

namespace deluge::project {

/// @brief The DOM-owned structural record for one clip.
///
/// Kept in `ClipStructure`, keyed by the clip's stable `Clip::id` -- never by a `Clip*` or array index.
///
/// @note Authoritative: the `Clip` has no copies of these fields. Its accessors (`loopLength()`/`colourOffset()`/
/// `section()`/`launchStyle()`) read and write this record directly through the clip's bound `docClip_`. Save
/// reads it via those accessors; load writes it (`readShellFromTree` + setters).
struct DocClip {
	int32_t loop_length{0};
	int16_t colour_offset{-60}; ///< matches the engine Clip default
	LaunchStyle launch_style{LaunchStyle::DEFAULT};
	uint8_t section{0};

	/// Per-note-row structural records, one per engine NoteRow. A std::list for STABLE element addresses (each
	/// engine NoteRow holds a raw pointer into this pool). Order within the list is irrelevant -- note-row order is
	/// a function of `y`, reproduced at save from the engine's y-ordered noteRows.
	std::list<DocNoteRow> note_rows;

	bool operator==(const DocClip&) const = default;
};

} // namespace deluge::project
