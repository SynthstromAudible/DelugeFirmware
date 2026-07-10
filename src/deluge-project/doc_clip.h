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

#include <cstdint>

namespace deluge::project {

/// The DOM-owned structural record for one clip (M3 owner-1 — the clip-structure ownership inversion, mirroring the
/// param/automation tier's DocAutoParam). Kept in `ClipStructure`, keyed by the clip's stable `Clip::id` — never
/// by a `Clip*` or array index.
///
/// AUTHORITATIVE: the `Clip` has no own copies of these fields — its accessors (`loopLength()`/`colourOffset()`/
/// `section()`/`launchStyle()`) read and write this record directly through the clip's bound `docClip_` (durable-id
/// Step 3). Save reads it (via those accessors), load writes it (`readShellFromTree` + setters); the ownership
/// inversion for clip structure is complete. `launch_style` stores the `LaunchStyle` enum's underlying value to
/// keep this model header engine-light.
struct DocClip {
	int32_t loop_length{0};
	int16_t colour_offset{-60}; ///< matches the engine Clip default
	uint8_t launch_style{0};    ///< LaunchStyle underlying value (0 = DEFAULT)
	uint8_t section{0};
};

} // namespace deluge::project
