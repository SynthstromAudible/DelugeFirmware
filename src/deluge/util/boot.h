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

#include "libdeluge/boot.h"

namespace deluge::boot {

/// @brief Whether boot has finished: the app's run loop has started, and storage calls go through the channels.
///
/// Before it, card I/O runs inline on the caller -- nothing is running yet to answer a channel request. After it,
/// the channels are the only place card I/O may start from. The unit tests, which link no BSP, never finish booting
/// in this sense, so they always take the inline arm.
///
/// Monotonic: false, then true for the rest of the process. Backed by the BSP's boot latch
/// (`deluge_boot_mark_finished`).
[[nodiscard]] inline bool finished() {
	return deluge_boot_finished();
}

} // namespace deluge::boot
