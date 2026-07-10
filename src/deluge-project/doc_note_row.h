/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include <cstdint>

namespace deluge::project {

/// The DOM-owned structural record for one engine NoteRow (M3 owner-2). Owned by its DocClip's `note_rows` pool
/// (stable address); the engine NoteRow holds a non-owning pointer back to it. Field values are seeded from the
/// engine row at bind time (NoteRow's colourOffset is random() at creation), not from these defaults.
struct DocNoteRow {
	int16_t y{0};
	int32_t loop_length_if_independent{0}; ///< 0 = obeys the parent clip's loop length
	int8_t colour_offset{0};
	uint8_t sequence_direction{0}; ///< SequenceDirection enum underlying value
	bool muted{false};
};

} // namespace deluge::project
