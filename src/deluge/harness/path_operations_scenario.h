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

/// @brief Host-only C ABI for the path-operations gate.
///
/// ## Two halves, two kinds of evidence
///
/// `PO-SYSEX` is the round trip: a real SysEx request for each converted verb must still produce its
/// `^verb` reply carrying the right `err`. It passes BEFORE the conversion as well as after, which is
/// the point — it is what catches a conversion that moved the I/O and broke the protocol.
///
/// `PO-TIER` is the browser half's evidence: folder creation begun on the interaction tier, non-zero
/// before the conversion and zero after. **`smsysex` has no equivalent**, because it is already
/// owner-dispatched, so its tier count reads zero on both sides and `PO-CHANNEL served=` is the whole
/// of its proof.
///
/// ## Replies are captured, not inferred
///
/// A loopback `MIDICable` records what `sendMsg` sent. Asserting the filesystem effect instead would
/// pass for a conversion that stopped replying altogether, which is exactly the failure deferring a
/// reply risks.
extern "C" {

/// Reset the scenario's state and prepare stdout for a piped run.
void deluge_scenario_path_operations_begin(void);

/// Whether the summary has been printed and the run may stop stepping.
bool deluge_scenario_path_operations_complete(void);

/// Run one tick of the scenario, then queue a marker behind whatever it dispatched.
void deluge_scenario_path_operations_dispatch_step(void);

/// Whether the marker queued by the last dispatch has run.
bool deluge_scenario_path_operations_step_done(void);

/// How many in-process assertions failed.
int32_t deluge_scenario_path_operations_failures(void);

} // extern "C"
