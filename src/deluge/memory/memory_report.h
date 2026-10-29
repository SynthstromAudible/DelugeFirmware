/*
 * Copyright © 2026 Synthstrom Audible Limited
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

namespace deluge::memory {

/// The word a debugger fills the program stack with before the image runs (tools/device_check.py). The stack's
/// lowest word still holding it says the stack was painted; the lowest word that no longer does is its high-water
/// mark.
inline constexpr uint32_t kStackPaint = 0x57ACC0DE;

/// @brief [task] Logs one `MEM` line -- each heap's free bytes and their low-water mark, and the program stack's
/// high-water mark -- when a debugger painted the stack; otherwise does nothing.
///
/// A plain run has no paint, so it pays one load and a compare per call and prints nothing.
void reportMemoryIfPainted();

} // namespace deluge::memory
