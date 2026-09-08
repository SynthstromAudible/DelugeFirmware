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

/// @brief Host-only C ABI for the startup fail-safe gate: drive the REAL fail-safe and require that
///        none of its file I/O begins on the interaction tier.
///
/// ## What it drives, and what it deliberately does not
///
/// The fail-safe's happy path ends in `openUI(&loadSongUI)`, and this harness has no UI stack and no
/// root UI, so `openUI` would index off the end of an array. The scenario therefore drives the
/// **song-missing** path: startup mode `LASTOPENED` with no startup song recorded and no
/// `SONGS/DEFAULT.XML`, which reaches the canary creation, all the existence checks and the cleanup
/// unlink, and then returns without touching the UI.
///
/// That is not a reduced test. Those are five of the fail-safe's file operations, and which TIER they
/// run on — the whole subject of this gate — does not depend on which branch reached them.
///
/// ## `SF-TIER` is the evidence; the rest is context
///
/// It is deliberately non-zero before the conversion, so the gate FAILS then, and zero after. That
/// transition is the only thing that proves anything moved. `SF-CHANNEL` is reported and not asserted
/// here: the default build has the byte channel compiled out and 0 is the honest answer, so it is
/// `scripts/startup_failsafe.sh`'s channel-on leg that requires it non-zero.
extern "C" {

/// Reset the scenario's state and prepare stdout for a piped run.
void deluge_scenario_startup_failsafe_begin(void);

/// Whether the summary has been printed and the run may stop stepping.
bool deluge_scenario_startup_failsafe_complete(void);

/// Run one tick of the scenario, then queue a marker behind whatever it dispatched.
void deluge_scenario_startup_failsafe_dispatch_step(void);

/// Whether the marker queued by the last dispatch has run.
bool deluge_scenario_startup_failsafe_step_done(void);

/// How many in-process assertions failed.
int32_t deluge_scenario_startup_failsafe_failures(void);

} // extern "C"
