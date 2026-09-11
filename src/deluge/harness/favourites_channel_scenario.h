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

/// @brief Host-only C ABI for the favourites-channel gate: drive the favourites bank through its real
///        entry points, require that no bank is ever written with less than it held, and count every
///        favourites card operation that began on the interaction tier.
///
/// Stepped, not one blocking call: once converted, a bank operation is only QUEUED when its entry point
/// runs, so every check waits for `FavouritesManager::idle()` -- never for a file to appear, which is a
/// proxy for an operation finishing and not the thing itself.
extern "C" {

/// Reset the scenario's state and prepare stdout for a piped run.
void deluge_scenario_favourites_channel_begin(void);

/// Whether the summary has been printed and the run may stop stepping.
bool deluge_scenario_favourites_channel_complete(void);

/// Drive the channel and the bank, run one tick of the scenario, then queue a marker behind it.
void deluge_scenario_favourites_channel_dispatch_step(void);

/// Whether the marker queued by the last dispatch has run.
bool deluge_scenario_favourites_channel_step_done(void);

/// How many in-process assertions failed.
int32_t deluge_scenario_favourites_channel_failures(void);

} // extern "C"
