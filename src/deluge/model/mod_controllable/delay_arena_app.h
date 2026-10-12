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

#include "engines/effects/delay_arena.h"

#include <cstdint>

namespace deluge::model {

/// @return The app's one delay arena: regions from the general SDRAM heap, each a BSP heap, maintained by the
/// "delay arena" app-loop job.
deluge::effects::DelayArena& delayArena();

/// @brief Adds the "delay arena" job to the app loop: no timer, woken by the render after it takes or gives back a
/// tape. Each run lends a region when the arena's margin is short and returns the regions it no longer needs.
void registerDelayArenaJob();

/// @return How many times the job has maintained the arena.
uint32_t delayArenaMaintenanceRuns();

} // namespace deluge::model
