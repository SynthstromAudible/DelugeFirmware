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

#include "model/scale/musical_key.h"
#include "model/scale/note_set.h"
#include "model/scale/preset_scales.h"
#include "model/scale/scale_mapper.h"
#include <bitset>

namespace deluge::project {

/// Scale/key settings of a song: the current key (root note + mode notes), which preset scales are
/// disabled, the user-defined scale, and the mapper that computes note remappings on a scale change.
/// A component of deluge::project::Song.
struct ScaleSettings {
	MusicalKey key;
	std::bitset<NUM_PRESET_SCALES> disabled_preset_scales;
	/// The song's user scale (empty when none is defined). Serialized as the "userScale" tag.
	NoteSet user_scale_notes;
	/// Computes per-note remappings when the scale changes; effectively private state of that path.
	ScaleMapper scale_mapper;

	/// Whether a user scale is currently defined.
	[[nodiscard]] bool hasUserScale() const { return !user_scale_notes.isEmpty(); }
};

} // namespace deluge::project
