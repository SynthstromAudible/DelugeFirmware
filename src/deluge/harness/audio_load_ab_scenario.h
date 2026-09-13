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

/// @brief Host-only C ABI for the sync-vs-async load A/B: run ONE whole song-shaped load of a fixed
///        sample list through `AudioFileManager::getAudioFileFromFilename` and print each file's
///        resolved identity and geometry as a stable line, checked against a synchronous reference.
///
/// Every case loads twice in one process: first through the synchronous card path (`mayLoadAsync=false`), as the
/// reference, and then through the Rust Embassy loader (`libdeluge/audio_load.h`'s request/collect pair). The async
/// load's `AB-CASE` line must equal the reference's. `AB-SUMMARY` carries the per-run facts, such as deferral counts,
/// that the reference has no counterpart for.
///
/// The whole point is that everything else must be identical: same file chosen, same alternate-dir
/// resolution, same geometry, same `Error` on failure, same bytes readable back through the
/// Sample's own stream.
///
/// @note Runs as a STEP MACHINE rather than one blocking call, because that is what the async
///       backing requires: a deferred load answers `Error::LOAD_IN_PROGRESS` and the caller must
///       return to its own loop so the Embassy loader task can be polled at all. Each
///       `dispatch_step()` runs one attempt on the storage-owner worker fiber (where card I/O is
///       allowed) and returns; the platform driver waits for `step_done()`, lets its executor run,
///       and dispatches again.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness directory is globbed into the
///       app's sources), so the Embassy host binary — which links `deluge_app`, not any of the
///       C-host `main()`s — can reach it. Inert unless called.
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

/// @brief Reset the case cursor and install the song-shaped alternate-dir state one load starts
///        from (`alternateAudioFileLoadPath` = "SAMPLES/ALT", status MIGHT_EXIST, thing type SONG).
/// @note Call once, before the first dispatch_step(). Safe on any context (no I/O).
void deluge_scenario_audio_load_ab_begin(void);

/// @brief True once every case has been recorded and the summary printed.
bool deluge_scenario_audio_load_ab_complete(void);

/// @brief Queue one load attempt for the current case onto the storage-owner worker fiber.
/// @note Fire-and-forget: poll deluge_scenario_audio_load_ab_step_done() for its completion.
void deluge_scenario_audio_load_ab_dispatch_step(void);

/// @brief True once the attempt queued by the last dispatch_step() has returned.
bool deluge_scenario_audio_load_ab_step_done(void);

/// @brief Number of in-process assertion failures (a load that differs from its synchronous
///        reference, invariant breaches, wedged loads, a case that never actually deferred).
///        0 = the run passed.
int32_t deluge_scenario_audio_load_ab_failures(void);

} // extern "C"

#endif // DELUGE_HOST
