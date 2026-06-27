/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

// The pure number formatting (intToString, …) and the board timer-count math
// (msToSlowTimerCount, …) moved to the foundation tier. This header forwards to
// them so existing includers keep working; new code should include the
// foundation headers directly. Only the busy-wait delays remain here — they wrap
// the libdeluge clock boundary, so they sit above the foundation.
#include "foundation/string_format.h"
#include "foundation/timer_count.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void delayMS(uint32_t ms);
void delayUS(uint32_t us);

#ifdef __cplusplus
}
#endif
