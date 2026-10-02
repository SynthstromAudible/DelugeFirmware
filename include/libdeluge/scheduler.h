/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

/// libdeluge/scheduler.h — the BSP clock the app reads.
///
/// The app's work runs as jobs in its own loop (src/deluge/runtime/app_loop.h), which the BSP drives
/// through deluge_app_service (app.h); the render runs on the BSP's audio tier
/// (deluge_app_audio_tick). Also declares the clock tick type and rate (`dTime`,
/// `DELUGE_CLOCKS_PER*`) that the C++ `Time` wrapper (`task_scheduler/clock_type.h`) is built on.
#ifndef LIBDELUGE_SCHEDULER_H
#define LIBDELUGE_SCHEDULER_H

#include "abi.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// A tick count at the board's fixed clock rate (see DELUGE_CLOCKS_PER).
typedef int64_t dTime;
#define DELUGE_CLOCKS_PER 33330000
#define DELUGE_CLOCKS_PERf 33330000.

/// Seconds elapsed since Embassy's monotonic clock started (`Instant::now()`) — effectively since
/// boot. [task] [audio]
double getSystemTime() DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif
#endif // LIBDELUGE_SCHEDULER_H
