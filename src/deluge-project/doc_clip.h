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
/// param/automation tier's DocAutoParam). Kept in `ClipStructure`, keyed by the clip's stable `Clip::serialId` (or a
/// transient sessionId) — never by a `Clip*` or array index.
///
/// SLICE 1 (shadow): owns only `loop_length` — the one clip-structure field with an Edit funnel (`SetClipLength`)
/// today. It is written alongside the engine `Clip::loopLength` (dual-write) but nothing reads it yet; the engine
/// field stays authoritative until the read/save/load flip. `section`/`colour_offset`/`launch_style` join this struct
/// as their own Edits land.
struct DocClip {
	int32_t loop_length{0};
};

} // namespace deluge::project
