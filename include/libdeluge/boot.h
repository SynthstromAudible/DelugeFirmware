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

/// libdeluge/boot.h -- the boot latch: whether the app's run loop has started, and storage calls go through the
/// channels. The BSP owns it and sets it once, right after `deluge_app_init` returns.

#ifndef LIBDELUGE_BOOT_H
#define LIBDELUGE_BOOT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Mark boot finished: the app's run loop has started, and storage calls go through the channels. Called
///        once by the BSP, right after `deluge_app_init` returns. Monotonic.
void deluge_boot_mark_finished(void);

/// @brief Whether boot has finished. False, then true for the rest of the process. The unit tests, which link no BSP,
///        never mark it, so it is always false there.
bool deluge_boot_finished(void);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_BOOT_H
