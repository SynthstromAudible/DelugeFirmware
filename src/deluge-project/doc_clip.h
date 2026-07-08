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
/// Shadow store: each field is written alongside its engine `Clip` field (dual-write) but nothing reads DocClip yet;
/// the engine fields stay authoritative until the read/save/load flip. Fields join as their gestures get Edit-funneled:
/// slice 1 = `loop_length` (`SetClipLength`); slice 2 = `colour_offset` (`SetClipColour`) + `launch_style`
/// (`SetClipLaunchStyle`). `section` joins when its (scattered) gesture is funneled. `launch_style` stores the
/// `LaunchStyle` enum's underlying value to keep this model header engine-light.
struct DocClip {
	int32_t loop_length{0};
	int16_t colour_offset{-60}; ///< matches the engine Clip default
	uint8_t launch_style{0};    ///< LaunchStyle underlying value (0 = DEFAULT)
};

} // namespace deluge::project
