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

/// @brief Host-only C ABI for the song-preview A/B: draw every fixture song's artwork through the
///        REAL `LoadSongUI`, under one of the two preview backings, and print each song's rendered
///        image and decode outcome as a stable, diffable line.
///
/// Which backing serves the preview is not a runtime choice: `deluge_file_prefix_async_active()` is
/// `cfg!(feature = "async_file_prefix_active")` on the Rust side, a compile-time constant. So the
/// A/B is across two BUILDS of the host harness, not two passes inside one process, and the
/// comparison is a diff of the two runs' `SP-CASE` lines. `scripts/song_preview_ab.sh` runs it.
///
/// ## What is compared, and why pixels alone are not enough
///
/// Each `SP-CASE` line carries a CRC of the rendered pad image AND the `SongPreviewOutcome` the
/// decode reported, plus the `Error` it put on screen. The outcome is not decoration: an unreadable
/// song, a song with no artwork, and a preview that falls past the read window all draw an
/// IDENTICAL blank screen, so a gate that compared only images would have passed while one backing
/// showed the user an error popup and the other showed nothing at all. That is exactly the state
/// the two backings were in when this gate was written.
///
/// ## The re-ask is under test, not simulated
///
/// Every case calls `LoadSongUI::drawSongPreview()` ONCE -- the shipped entry a cursor move
/// reaches -- and from then on drives only `LoadSongUI::graphicsRoutine()`, the per-tick surface,
/// with no further input of any kind. Nothing here runs a retry loop of its own. If the browser
/// ever stops re-asking for a preview whose bytes have not arrived, the async leg simply never
/// lands and every one of its cases fails.
///
/// ## Deferrals are asserted PER CASE
///
/// A run-wide deferral total is satisfied by one deferral anywhere, which would leave the async
/// path inert for every other song while the gate stayed green. Each case carries its own count and
/// each is required to be non-zero on the async leg.
///
/// @note Runs as a STEP MACHINE rather than one blocking call, because that is what the async
///       backing requires: a preview whose prefix has not arrived is answered on a later tick, and
///       the caller must return to its own loop for the Embassy reader task to be polled at all.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness directory is globbed into the
///       app's sources), so the Embassy host binary -- which links `deluge_app`, not any of the
///       C-host `main()`s -- can reach it. Inert unless called.
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

/// @brief Reset the case cursor and put the browser into the state a song listing leaves it in.
/// @note Call once, before the first dispatch_step(). Safe on any context (no I/O).
void deluge_scenario_song_preview_ab_begin(void);

/// @brief True once every case has been recorded and the summary printed.
bool deluge_scenario_song_preview_ab_complete(void);

/// @brief Advance the scenario by one tick. Fire-and-forget; poll step_done() for completion.
void deluge_scenario_song_preview_ab_dispatch_step(void);

/// @brief True once the work queued by the last dispatch_step() has returned.
bool deluge_scenario_song_preview_ab_step_done(void);

/// @brief Number of in-process assertion failures. 0 = this run is internally consistent; the
///        cross-build diff is a separate, and the primary, check.
int32_t deluge_scenario_song_preview_ab_failures(void);

} // extern "C"

#endif // DELUGE_HOST
