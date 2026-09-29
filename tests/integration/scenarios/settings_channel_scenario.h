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

/// @brief Host-only C ABI for the settings-channel gate: write a settings file through the REAL
///        consumer entry point, read it back, and require the value to survive -- while counting
///        every settings operation that began on the interaction tier.
///
/// ## Two halves
///
/// The round trip is the SAFETY half: it catches a change that moved the I/O and broke the settings
/// on the way. `SC-TIER` is the EVIDENCE half: it must be zero, and it is the only check that shows
/// where the I/O ran.
///
/// A round trip alone is not enough: it passes the same whichever tier did the I/O, so a gate with
/// only a round trip would report success with every settings operation still running on the
/// interaction tier.
///
/// ## Why the count and not a dispatch counter
///
/// A counter incremented at the point of dispatch proves that dispatch was reached, not that the
/// work moved. This one is incremented inside the consumer's own I/O path, from
/// `deluge::storage::bytes::noteIoTier()`, and it reads the tier it is actually standing
/// on -- so it cannot report success for an operation that dispatched and then ran inline anyway.
///
/// ## Stepped, not one blocking call
///
/// After boot settings operations are channel-backed, so the work is only ENQUEUED when the entry
/// point runs. A same-tick read would prove nothing, so each stage has
/// a `*Check` successor a tick later.
extern "C" {

/// @brief Reset the scenario's state and prepare stdout for a piped run.
void deluge_scenario_settings_channel_begin(void);

/// @brief Whether the summary has been printed and the run may stop stepping.
bool deluge_scenario_settings_channel_complete(void);

/// @brief Run one tick of the scenario, then queue a marker behind whatever it dispatched.
void deluge_scenario_settings_channel_dispatch_step(void);

/// @brief Whether the marker queued by the last dispatch has run.
bool deluge_scenario_settings_channel_step_done(void);

/// @brief How many in-process assertions failed.
int32_t deluge_scenario_settings_channel_failures(void);

} // extern "C"
