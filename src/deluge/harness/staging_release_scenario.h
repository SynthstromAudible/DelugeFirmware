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

/// @file
/// @brief The staging-release gate: releasing a staged document while the channel is still reading into it.
///
/// The write channel's whole-file read writes into the caller's buffer from the storage tier and cannot be cancelled,
/// so a consumer that abandons a staged read -- a preset load superseded by a newer one, a song load given up -- must
/// not have that buffer freed under the read. The scenario holds a staged read in flight, releases its staging as an
/// abandoning consumer does, allocates the same size again and fills it with a sentinel, and lets the read finish. The
/// sentinel must survive: memory handed to someone else may not be written by a read its previous owner gave up on.
/// The released buffer must then still be freed once the read has finished, or the release has only traded the
/// corruption for a leak.
extern "C" {

/// @brief Reset the scenario's state and prepare stdout for a piped run.
void deluge_scenario_staging_release_begin(void);

/// @brief Whether the summary has been printed and the run may stop stepping.
bool deluge_scenario_staging_release_complete(void);

/// @brief Run one tick of the scenario.
void deluge_scenario_staging_release_dispatch_step(void);

/// @brief Whether the last dispatched tick has finished.
bool deluge_scenario_staging_release_step_done(void);

/// @brief How many in-process assertions failed.
int32_t deluge_scenario_staging_release_failures(void);

} // extern "C"
